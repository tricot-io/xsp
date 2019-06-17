// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

// WS (WebSocket) client example.

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "xsp_ws_client.h"
#include "xsp_ws_client_defrag.h"
#include "xsp_ws_client_loop.h"

#include "app_wifi.h"

static const char TAG[] = "MAIN";

typedef struct {
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

static void do_sleep(int ms) {
    vTaskDelay((ms + (portTICK_PERIOD_MS - 1)) / portTICK_PERIOD_MS);
}

static void loop_event_handler(xsp_ws_client_loop_handle_t loop,
                               void* raw_ctx,
                               const xsp_ws_client_loop_event_t* evt) {
    loop_context_t* ctx = (loop_context_t*)raw_ctx;

    switch (evt->type) {
    case XSP_WS_CLIENT_LOOP_EVENT_IDLE: {
        ESP_LOGD(TAG, "Event: idle");  // This is super noisy.
        if (ctx->sent >= CONFIG_MAIN_NUM_SENDS) {
            if (ctx->done_idles >= CONFIG_MAIN_NUM_DONE_IDLES) {
                ESP_LOGI(TAG, "Closing");
                esp_err_t err = xsp_ws_client_loop_close(loop, XSP_WS_STATUS_CLOSE_NORMAL_CLOSURE);
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "Failed to close loop: %s", esp_err_to_name(err));
            } else {
                if (ctx->done_idles % CONFIG_MAIN_DONE_IDLE_PING_INTERVAL == 0) {
                    ESP_LOGI(TAG, "Pinging");
                    esp_err_t err = xsp_ws_client_loop_ping(loop, 2, "hi");
                    if (err != ESP_OK)
                        ESP_LOGE(TAG, "Failed to ping: %s", esp_err_to_name(err));
                } else {
                    ESP_LOGD(TAG, "  --> idling (done)");
                    do_sleep(CONFIG_MAIN_IDLE_SLEEP_MS);
                }
                ctx->done_idles++;
            }
        } else {
            if (ctx->sending) {
                ESP_LOGD(TAG, "  --> idling (still sending)");
                do_sleep(CONFIG_MAIN_IDLE_SLEEP_MS);
            } else {
                ESP_LOGI(TAG, "Sending message");
                int len = sprintf(ctx->send_buf, "hello %d", ctx->sent + 1);

                esp_err_t err = xsp_ws_client_loop_send_message(loop, false, len, ctx->send_buf);
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "Failed to send message: %s", esp_err_to_name(err));
                else
                    ctx->sending = true;
            }
        }
        break;
    }

    case XSP_WS_CLIENT_LOOP_EVENT_CLOSED: {
        ESP_LOGI(TAG, "Event: closed");
        const struct xsp_ws_client_loop_event_closed* data = &evt->data.closed;
        ESP_LOGI(TAG, "  status=%d", (int)data->status);
        break;
    }

    case XSP_WS_CLIENT_LOOP_EVENT_DATA_FRAME_RECEIVED: {
        ESP_LOGI(TAG, "Event: data frame received");
        const struct xsp_ws_client_loop_event_data_frame_received* data =
                &evt->data.data_frame_received;
        char buf[CONFIG_MAIN_TRUNC_SIZE + 1];
        trunc_buf(buf, data->payload_size, data->payload);
        ESP_LOGI(TAG, "  fin=%d, opcode=0x%02x, payload_size=%d, payload=\"%s\"%s", (int)data->fin,
                 (unsigned)data->opcode, data->payload_size, buf, trunc_dots(data->payload_size));

        bool done;
        xsp_ws_frame_opcode_t message_opcode;
        int message_size;
        void* message_data;
        esp_err_t err = xsp_ws_client_defrag_on_data_frame(ctx->defrag, data->fin, data->opcode,
                                                           data->payload_size, data->payload, &done,
                                                           &message_opcode, &message_size,
                                                           &message_data);
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
        break;
    }

    case XSP_WS_CLIENT_LOOP_EVENT_PING_RECEIVED: {
        ESP_LOGI(TAG, "Event: ping received");
        const struct xsp_ws_client_loop_event_ping_received* data = &evt->data.ping_received;
        char buf[CONFIG_MAIN_TRUNC_SIZE + 1];
        trunc_buf(buf, data->payload_size, data->payload);
        ESP_LOGI(TAG, "  payload_size=%d, payload=\"%s\"%s", data->payload_size, buf,
                 trunc_dots(data->payload_size));
        break;
    }

    case XSP_WS_CLIENT_LOOP_EVENT_PONG_RECEIVED: {
        ESP_LOGI(TAG, "Event: pong received");
        const struct xsp_ws_client_loop_event_pong_received* data = &evt->data.pong_received;
        char buf[CONFIG_MAIN_TRUNC_SIZE + 1];
        trunc_buf(buf, data->payload_size, data->payload);
        ESP_LOGI(TAG, "  payload_size=%d, payload=\"%s\"%s", data->payload_size, buf,
                 trunc_dots(data->payload_size));
        break;
    }

    case XSP_WS_CLIENT_LOOP_EVENT_MESSAGE_SENT: {
        ESP_LOGI(TAG, "Event: message sent");
        const struct xsp_ws_client_loop_event_message_sent* data = &evt->data.message_sent;
        ESP_LOGI(TAG, "  success=%d", (int)data->success);
        ctx->sending = false;
        ctx->sent++;
        break;
    }
    }
}

static void ws_client_example_task(void* pvParameters) {
    app_wifi_wait_connected();
    ESP_LOGI(TAG, "WiFi connected");

    ESP_LOGI(TAG, "Starting example");

    esp_err_t err;
    xsp_ws_client_handle_t client = NULL;
    loop_context_t ctx = {};
    xsp_ws_client_loop_handle_t loop = NULL;

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

    loop = xsp_ws_client_loop_init(NULL, client, &loop_event_handler, &ctx);
    if (!loop) {
        ESP_LOGE(TAG, "Failed to initialize loop");
        goto done;
    }

    err = xsp_ws_client_loop_run(loop);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to run loop: %s", esp_err_to_name(err));
        goto done;
    }

    // TODO(vtl): Our teardown is a bit out-of-order here. Hrm.
    err = xsp_ws_client_close(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to close client: %s", esp_err_to_name(err));
        goto done;
    }

    ESP_LOGI(TAG, "Yay!");

done:
    if (loop) {
        err = xsp_ws_client_loop_cleanup(loop);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to clean up loop: %s", esp_err_to_name(err));
    }

    if (ctx.defrag) {
        err = xsp_ws_client_defrag_cleanup(ctx.defrag);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to clean up defragmenter: %s", esp_err_to_name(err));
    }

    if (client) {
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
