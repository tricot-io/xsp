// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

// WS (WebSocket) client test (Autobahn|Testsuite client).
//
// For clarity, suppose that the Autobahn WebSocket Fuzzing Server ("wstest -m fuzzingserver") is
// running at wstest:9001. This does the following:
// *   Connect to ws://wstest:9001/getCaseCount. It should get a text message containing a
//     non-negative integer NUM_CASES and then be disconnected.
// *   For CASE_ID from 1 to NUM_CASES:
//     *   Connect to ws://wstest:9001/runCase?case=CASE_ID&agent=xsp_ws_client.
//     *   Echo messages until disconnected (possibly by our side, due to a detected error
//         condition).
// *   Connect to ws://wstest:9001/updateReports?agent=xsp_ws_client", It should be disconnected,
//     but this should trigger report (re)generation.
// *   Stop.

#include <stdbool.h>
#include <stdint.h>
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

static void do_sleep(int ms) {
    vTaskDelay((ms + (portTICK_PERIOD_MS - 1)) / portTICK_PERIOD_MS);
}

static void check_failed() {
    // Don't reset, since that leads to looping.
    for (;;)
        do_sleep(1000);
}

#define CHECK_ERROR(x)                                                                 \
    do {                                                                               \
        esp_err_t err = x;                                                             \
        if (err != ESP_OK) {                                                           \
            ESP_LOGE(TAG, "%s:%d: Check error failed: %s: %s", __FILE__, __LINE__, #x, \
                     esp_err_to_name(err));                                            \
            check_failed();                                                            \
        }                                                                              \
    } while (0)

#define CHECK(x)                                                              \
    do {                                                                      \
        if (!(x)) {                                                           \
            ESP_LOGE(TAG, "%s:%d: Check failed: %s", __FILE__, __LINE__, #x); \
            check_failed();                                                   \
        }                                                                     \
    } while (0)

typedef struct do_echo_context {
    bool save_first_msg;
    int* first_msg_size;
    void** first_msg;

    xsp_ws_client_defrag_handle_t defrag;
    int sending_msg_size;
    void* sending_msg;
} do_echo_context_t;

static void on_loop_start(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGI(TAG, "Event: started");
}

static void on_loop_stop(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGI(TAG, "Event: stopped");
}

static void on_loop_idle(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGD(TAG, "Event: idle");
}

static void on_ws_client_closed(xsp_ws_client_handler_handle_t handler, void* raw_ctx, int status) {
    ESP_LOGD(TAG, "Event: closed");

    CHECK_ERROR(xsp_loop_stop(xsp_ws_client_handler_get_loop(handler)));
}

static void on_message(do_echo_context_t* ctx,
                       xsp_ws_client_handler_handle_t handler,
                       xsp_ws_frame_opcode_t message_opcode,
                       int message_size,
                       void* message_data) {
    CHECK(!message_size || !!message_data);

    bool binary;
    switch (message_opcode) {
    case XSP_WS_FRAME_OPCODE_TEXT:
        binary = false;
        break;
    case XSP_WS_FRAME_OPCODE_BINARY:
        binary = true;
        break;
    default:
        xsp_ws_client_handler_close(handler, XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR);
        return;
    }

    if (ctx->save_first_msg) {
        if (ctx->first_msg_size) {
            *ctx->first_msg_size = message_size;
            ctx->first_msg_size = NULL;
        }
        if (ctx->first_msg) {
            *ctx->first_msg = message_data;
            ctx->first_msg = NULL;
        } else {
            free(message_data);
        }
        return;
    }

    if (xsp_ws_client_handler_send_message(handler, binary, message_size, message_data) != ESP_OK) {
        free(message_data);
        return;
    }

    ctx->sending_msg_size = message_size;
    ctx->sending_msg = message_data;
}

static void on_ws_client_data_frame_received(xsp_ws_client_handler_handle_t handler,
                                             void* raw_ctx,
                                             bool fin,
                                             xsp_ws_frame_opcode_t opcode,
                                             int payload_size,
                                             const void* payload) {
    do_echo_context_t* ctx = (do_echo_context_t*)raw_ctx;

    ESP_LOGD(TAG, "Event: data frame received");

    bool done;
    xsp_ws_frame_opcode_t message_opcode;
    int message_size;
    void* message_data;
    esp_err_t err = xsp_ws_client_defrag_on_data_frame(ctx->defrag, fin, opcode, payload_size,
                                                       payload, &done, &message_opcode,
                                                       &message_size, &message_data);
    switch (err) {
    case ESP_OK:
        if (done)
            on_message(ctx, handler, message_opcode, message_size, message_data);
        break;

    case ESP_ERR_NO_MEM:
    case ESP_ERR_INVALID_SIZE:
        xsp_ws_client_handler_close(handler, XSP_WS_STATUS_CLOSE_MESSAGE_TOO_BIG);
        break;

    case ESP_FAIL:
        xsp_ws_client_handler_close(handler, XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR);
        break;

    case ESP_ERR_INVALID_RESPONSE:
        xsp_ws_client_handler_close(handler, XSP_WS_STATUS_CLOSE_INVALID_DATA);
        break;

    default:
        CHECK(false);
        break;
    }
}

static void on_ws_client_ping_received(xsp_ws_client_handler_handle_t handler,
                                       void* raw_ctx,
                                       int payload_size,
                                       const void* payload) {
    ESP_LOGD(TAG, "Event: ping received");
}

static void on_ws_client_pong_received(xsp_ws_client_handler_handle_t handler,
                                       void* raw_ctx,
                                       int payload_size,
                                       const void* payload) {
    ESP_LOGD(TAG, "Event: pong received");
}

static void on_ws_client_message_sent(xsp_ws_client_handler_handle_t handler,
                                      void* raw_ctx,
                                      bool success) {
    do_echo_context_t* ctx = (do_echo_context_t*)raw_ctx;

    ESP_LOGD(TAG, "Event: message sent");

    free(ctx->sending_msg);
    ctx->sending_msg_size = 0;
    ctx->sending_msg = NULL;
}

// In save-first-message mode, it just saves the first message (and doesn't echo).
static void do_echo(const char* url, bool save_first_msg, int* first_msg_size, void** first_msg) {
    xsp_ws_client_defrag_config_t defrag_config = {
            .max_message_size = 65536,
    };
    xsp_ws_client_config_t client_config = {
            .url = url,
    };
    xsp_ws_client_handler_config_t client_handler_config = xsp_ws_client_handler_config_default;
    client_handler_config.max_frame_read_size = 65536;

    do_echo_context_t ctx = {save_first_msg, first_msg_size, first_msg, NULL, 0, NULL, false};
    xsp_loop_event_handler_t loop_evt_handler = {&on_loop_start, &on_loop_stop, &on_loop_idle,
                                                 &ctx};
    xsp_ws_client_event_handler_t client_evt_handler = {
            &on_ws_client_closed,        &on_ws_client_data_frame_received,
            &on_ws_client_ping_received, &on_ws_client_pong_received,
            &on_ws_client_message_sent,  &ctx};

    xsp_ws_client_handle_t client = NULL;
    xsp_loop_handle_t loop = NULL;
    xsp_ws_client_handler_handle_t client_handler = NULL;

    ESP_LOGI(TAG, "    Connecting to %s....", url);

    CHECK(!!(ctx.defrag = xsp_ws_client_defrag_init(&defrag_config)));
    CHECK(!!(client = xsp_ws_client_init(&client_config)));
    CHECK(!!(loop = xsp_loop_init(NULL, &loop_evt_handler)));

    CHECK_ERROR(xsp_ws_client_open(client));
    CHECK(!!(client_handler = xsp_ws_client_handler_init(&client_handler_config,
                                                         &client_evt_handler, client, loop)));
    CHECK_ERROR(xsp_loop_run(loop));
    CHECK_ERROR(xsp_ws_client_close(client));

    CHECK_ERROR(xsp_ws_client_handler_cleanup(client_handler));
    CHECK_ERROR(xsp_loop_cleanup(loop));
    CHECK_ERROR(xsp_ws_client_cleanup(client));
    CHECK_ERROR(xsp_ws_client_defrag_cleanup(ctx.defrag));

    ESP_LOGI(TAG, "    Disconnected from %s.", url);
}

static void ws_client_testsuite_task(void* pvParameters) {
    static const char kBaseUrl[] = CONFIG_MAIN_AUTOBAHN_SERVER_BASE_URL;
    char url[200];

    app_wifi_wait_connected();
    ESP_LOGI(TAG, "WiFi connected");

    ESP_LOGI(TAG, "Starting testsuite....");

    ESP_LOGI(TAG, "  Getting number of test cases....");
    ESP_LOGI(TAG, "    Free heap: %u.", (unsigned)esp_get_free_heap_size());
    CHECK(snprintf(url, sizeof(url), "%s/getCaseCount", kBaseUrl) < sizeof(url));
    int first_msg_size = 0;
    void* first_msg = NULL;
    do_echo(url, true, &first_msg_size, &first_msg);
    CHECK(!!first_msg);
    int num_cases;
    {
        char buf[10 + 1];
        memcpy(buf, first_msg, (first_msg_size > 10) ? 10 : first_msg_size);
        buf[first_msg_size] = '\0';
        free(first_msg);
        num_cases = atoi(buf);
    }
    CHECK(num_cases >= 0);
    ESP_LOGI(TAG, "    Free heap: %u.", (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Number of test cases: %d.", num_cases);

    for (int case_id = 1; case_id <= num_cases; case_id++) {
        ESP_LOGI(TAG, "  Starting test case %d of %d....", case_id, num_cases);
        ESP_LOGI(TAG, "    Free heap: %u.", (unsigned)esp_get_free_heap_size());
        CHECK(snprintf(url, sizeof(url), "%s/runCase?case=%d&agent=xsp_ws_client", kBaseUrl,
                       case_id) < sizeof(url));
        do_echo(url, false, NULL, NULL);
        ESP_LOGI(TAG, "    Free heap: %u.", (unsigned)esp_get_free_heap_size());
        ESP_LOGI(TAG, "  Finished test case %d of %d.", case_id, num_cases);
    }

    ESP_LOGI(TAG, "  Updating reports....");
    ESP_LOGI(TAG, "    Free heap: %u.", (unsigned)esp_get_free_heap_size());
    CHECK(snprintf(url, sizeof(url), "%s/updateReports?agent=xsp_ws_client", kBaseUrl) <
          sizeof(url));
    do_echo(url, true, NULL, NULL);
    ESP_LOGI(TAG, "    Free heap: %u.", (unsigned)esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Finished reports.");

    ESP_LOGI(TAG, "Finished testsuite.");

    vTaskDelete(NULL);
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        CHECK_ERROR(nvs_flash_erase());
        err = nvs_flash_init();
    }
    CHECK_ERROR(err);

    app_wifi_initialise();

    xTaskCreate(&ws_client_testsuite_task, "ws_client_testsuite_task", 8192, NULL, 5, NULL);
}
