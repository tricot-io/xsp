// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

// WS (WebSocket) client example.

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "xsp_loop.h"
#include "xsp_ws_client.h"
#include "xsp_ws_client_defrag.h"
#include "xsp_ws_client_handler.h"

#include "app_wifi.h"

static const char TAG[] = "MAIN";

typedef struct {
    xsp_ws_client_handler_handle_t client_handler;
    xsp_ws_client_defrag_handle_t defrag;
    bool sending;
    int sent;
    char send_buf[100];
    int done_idles;
} loop_context_t;

static void trunc_buf(char* dest, int size, const void* src) {
    if (size > CONFIG_MAIN_TRUNC_SIZE)
        size = CONFIG_MAIN_TRUNC_SIZE;
    memcpy(dest, src, size);
    dest[size] = '\0';
}

static const char* trunc_dots(int size) {
    return (size > CONFIG_MAIN_TRUNC_SIZE) ? "..." : "";
}

static void on_loop_start(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGI(TAG, "Event: started");
}

static void on_loop_stop(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGI(TAG, "Event: stopped");
}

static void on_loop_idle(xsp_loop_handle_t loop, void* raw_ctx) {
    loop_context_t* ctx = (loop_context_t*)raw_ctx;

    ESP_LOGD(TAG, "Event: idle");  // This is super noisy.

    if (ctx->sent >= CONFIG_MAIN_NUM_SENDS) {
        if (ctx->done_idles >= CONFIG_MAIN_NUM_DONE_IDLES) {
            // TODO(vtl): Currently, we should get the closed event in the next cycle, so this
            // should only happen once, but it's a bit shady.
            ESP_LOGI(TAG, "Closing");
            esp_err_t err = xsp_ws_client_handler_close(ctx->client_handler,
                                                        XSP_WS_STATUS_CLOSE_NORMAL_CLOSURE);
            if (err != ESP_OK)
                ESP_LOGE(TAG, "Failed to close handler: %s", esp_err_to_name(err));
            // We'll stop the loop in on_ws_client_closed().
        } else {
            if (ctx->done_idles % CONFIG_MAIN_DONE_IDLE_PING_INTERVAL == 0) {
                ESP_LOGI(TAG, "Pinging");
                esp_err_t err = xsp_ws_client_handler_ping(ctx->client_handler, 2, "hi");
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "Failed to ping: %s", esp_err_to_name(err));
            } else {
                ESP_LOGD(TAG, "  --> idling (done)");
            }
            ctx->done_idles++;
        }
    } else {
        if (ctx->sending) {
            ESP_LOGD(TAG, "  --> idling (still sending)");
        } else {
            ESP_LOGI(TAG, "Sending message");
            int len = sprintf(ctx->send_buf, "hello %d", ctx->sent + 1);

            esp_err_t err = xsp_ws_client_handler_send_message(ctx->client_handler, false, len,
                                                               ctx->send_buf);
            if (err != ESP_OK)
                ESP_LOGE(TAG, "Failed to send message: %s", esp_err_to_name(err));
            else
                ctx->sending = true;
        }
    }
}

static void on_ws_client_closed(xsp_ws_client_handler_handle_t handler, void* raw_ctx, int status) {
    ESP_LOGI(TAG, "Event: closed");

    ESP_LOGI(TAG, "  status=%d", status);
    esp_err_t err = xsp_loop_stop(xsp_ws_client_handler_get_loop(handler));
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Failed to stop loop: %s", esp_err_to_name(err));
}

static void on_ws_client_data_frame_received(xsp_ws_client_handler_handle_t handler,
                                             void* raw_ctx,
                                             bool fin,
                                             xsp_ws_frame_opcode_t opcode,
                                             int payload_size,
                                             const void* payload) {
    loop_context_t* ctx = (loop_context_t*)raw_ctx;

    ESP_LOGI(TAG, "Event: data frame received");

    char buf[CONFIG_MAIN_TRUNC_SIZE + 1];
    trunc_buf(buf, payload_size, payload);
    ESP_LOGI(TAG, "  fin=%d, opcode=0x%02x, payload_size=%d, payload=\"%s\"%s", (int)fin,
             (unsigned)opcode, payload_size, buf, trunc_dots(payload_size));

    bool done;
    xsp_ws_frame_opcode_t message_opcode;
    int message_size;
    void* message_data;
    esp_err_t err = xsp_ws_client_defrag_on_data_frame(ctx->defrag, fin, opcode, payload_size,
                                                       payload, &done, &message_opcode,
                                                       &message_size, &message_data);
    if (done) {
        if (err == ESP_OK) {
            trunc_buf(buf, message_size, message_data);
            ESP_LOGI(TAG, "  message complete: opcode=%d, size=%d, data=\"%s\"%s",
                     (int)message_opcode, message_size, buf, trunc_dots(message_size));
            free(message_data);
        } else {
            ESP_LOGI(TAG, "  message complete: error=%d", (int)err);
        }
    }
    // TODO(vtl): Report error if not done?
}

static void on_ws_client_ping_received(xsp_ws_client_handler_handle_t handler,
                                       void* raw_ctx,
                                       int payload_size,
                                       const void* payload) {
    ESP_LOGI(TAG, "Event: ping received");

    char buf[CONFIG_MAIN_TRUNC_SIZE + 1];
    trunc_buf(buf, payload_size, payload);
    ESP_LOGI(TAG, "  payload_size=%d, payload=\"%s\"%s", payload_size, buf,
             trunc_dots(payload_size));
}

static void on_ws_client_pong_received(xsp_ws_client_handler_handle_t handler,
                                       void* raw_ctx,
                                       int payload_size,
                                       const void* payload) {
    ESP_LOGI(TAG, "Event: pong received");

    char buf[CONFIG_MAIN_TRUNC_SIZE + 1];
    trunc_buf(buf, payload_size, payload);
    ESP_LOGI(TAG, "  payload_size=%d, payload=\"%s\"%s", payload_size, buf,
             trunc_dots(payload_size));
}

static void on_ws_client_message_sent(xsp_ws_client_handler_handle_t handler,
                                      void* raw_ctx,
                                      bool success) {
    loop_context_t* ctx = (loop_context_t*)raw_ctx;

    ESP_LOGI(TAG, "Event: message sent");

    ESP_LOGI(TAG, "  success=%d", (int)success);
    ctx->sending = false;
    ctx->sent++;
}

static void ws_client_example_task(void* pvParameters) {
    app_wifi_wait_connected();
    ESP_LOGI(TAG, "WiFi connected");

    ESP_LOGI(TAG, "Starting example");

    esp_err_t err;
    xsp_ws_client_handle_t client = NULL;
    loop_context_t ctx = {};
    xsp_loop_event_handler_t loop_evt_handler = {&on_loop_start, &on_loop_stop, &on_loop_idle, NULL,
                                                 &ctx};
    xsp_loop_handle_t loop = NULL;
    xsp_ws_client_event_handler_t client_evt_handler = {
            &on_ws_client_closed,        &on_ws_client_data_frame_received,
            &on_ws_client_ping_received, &on_ws_client_pong_received,
            &on_ws_client_message_sent,  &ctx};
    static const char kUrl[] = CONFIG_MAIN_URL;
    static const char kSubprotocols[] = CONFIG_MAIN_SUBPROTOCOLS;
    bool have_subprotocols = strlen(kSubprotocols) > 0;
    ESP_LOGI(TAG, "Will connect to: %s", kUrl);
    if (have_subprotocols)
        ESP_LOGI(TAG, "Will request subprotocols: %s", kSubprotocols);

    xsp_ws_client_config_t client_config = {
            .url = kUrl,
            .subprotocols = have_subprotocols ? kSubprotocols : NULL,
    };
    client = xsp_ws_client_init(&client_config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize client");
        goto done;
    }

    err = xsp_ws_client_open(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open client: %s", esp_err_to_name(err));
        goto done;
    }

    if (have_subprotocols) {
        ESP_LOGI(TAG, "Got server response subprotocols: %s",
                 xsp_ws_client_get_response_subprotocols(client));
    }

    static const xsp_ws_client_defrag_config_t kDefragConfig = {
            .max_message_size = 65536,
    };
    ctx.defrag = xsp_ws_client_defrag_init(&kDefragConfig);
    if (!ctx.defrag) {
        ESP_LOGE(TAG, "Failed to initialize defragmenter");
        goto done;
    }

    loop = xsp_loop_init(NULL, &loop_evt_handler);
    if (!loop) {
        ESP_LOGE(TAG, "Failed to initialize loop");
        goto done;
    }

    ctx.client_handler = xsp_ws_client_handler_init(NULL, &client_evt_handler, client, loop);
    if (!ctx.client_handler) {
        ESP_LOGE(TAG, "Failed to initialize client handler");
        goto done;
    }

    err = xsp_loop_run(loop);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to run loop: %s", esp_err_to_name(err));
        goto done;
    }

    ESP_LOGI(TAG, "Yay!");

done:
    if (ctx.client_handler) {
        err = xsp_ws_client_handler_cleanup(ctx.client_handler);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to clean up client handler: %s", esp_err_to_name(err));
    }

    if (loop) {
        err = xsp_loop_cleanup(loop);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to clean up loop: %s", esp_err_to_name(err));
    }

    if (ctx.defrag) {
        err = xsp_ws_client_defrag_cleanup(ctx.defrag);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to clean up defragmenter: %s", esp_err_to_name(err));
    }

    if (client) {
        // TODO(vtl): This happens even if we didn't open....
        err = xsp_ws_client_close(client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to close client: %s", esp_err_to_name(err));
            goto done;
        }

        err = xsp_ws_client_cleanup(client);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to clean up client: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Finished example");

    vTaskDelete(NULL);
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    app_wifi_initialise();

    xTaskCreate(&ws_client_example_task, "ws_client_example_task", 8192, NULL, 5, NULL);
}
