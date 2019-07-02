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

#include "xsp_ws_client.h"
#include "xsp_ws_client_defrag.h"
#include "xsp_ws_client_loop.h"

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

static void on_message(do_echo_context_t* ctx,
                       xsp_ws_client_loop_handle_t loop,
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
        xsp_ws_client_loop_close(loop, XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR);
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

    if (xsp_ws_client_loop_send_message(loop, binary, message_size, message_data) != ESP_OK) {
        free(message_data);
        return;
    }

    ctx->sending_msg_size = message_size;
    ctx->sending_msg = message_data;
}

static void do_echo_event_handler(xsp_ws_client_loop_handle_t loop,
                                  void* raw_ctx,
                                  const xsp_ws_client_loop_event_t* evt) {
    do_echo_context_t* ctx = (do_echo_context_t*)raw_ctx;

    switch (evt->type) {
    case XSP_WS_CLIENT_LOOP_EVENT_STARTED:
        ESP_LOGD(TAG, "Event: started");
        break;

    case XSP_WS_CLIENT_LOOP_EVENT_STOPPED:
        ESP_LOGD(TAG, "Event: stopped");
        break;

    case XSP_WS_CLIENT_LOOP_EVENT_IDLE:
        ESP_LOGD(TAG, "Event: idle");
        do_sleep(CONFIG_MAIN_IDLE_SLEEP_MS);
        break;

    case XSP_WS_CLIENT_LOOP_EVENT_CLOSED:
        ESP_LOGD(TAG, "Event: closed");
        break;

    case XSP_WS_CLIENT_LOOP_EVENT_DATA_FRAME_RECEIVED: {
        ESP_LOGD(TAG, "Event: data frame received");
        const struct xsp_ws_client_loop_event_data_frame_received* data =
                &evt->data.data_frame_received;

        bool done;
        xsp_ws_frame_opcode_t message_opcode;
        int message_size;
        void* message_data;
        esp_err_t err = xsp_ws_client_defrag_on_data_frame(
                ctx->defrag, data->fin, data->opcode, data->payload_size, data->payload, &done,
                &message_opcode, &message_size, &message_data);
        switch (err) {
        case ESP_OK:
            if (done)
                on_message(ctx, loop, message_opcode, message_size, message_data);
            break;

        case ESP_ERR_NO_MEM:
        case ESP_ERR_INVALID_SIZE:
            xsp_ws_client_loop_close(loop, XSP_WS_STATUS_CLOSE_MESSAGE_TOO_BIG);
            break;

        case ESP_FAIL:
            xsp_ws_client_loop_close(loop, XSP_WS_STATUS_CLOSE_PROTOCOL_ERROR);
            break;

        case ESP_ERR_INVALID_RESPONSE:
            xsp_ws_client_loop_close(loop, XSP_WS_STATUS_CLOSE_INVALID_DATA);
            break;

        default:
            CHECK(false);
            break;
        }
        break;
    }

    case XSP_WS_CLIENT_LOOP_EVENT_PING_RECEIVED:
        ESP_LOGD(TAG, "Event: ping received");
        break;

    case XSP_WS_CLIENT_LOOP_EVENT_PONG_RECEIVED:
        ESP_LOGD(TAG, "Event: pong received");
        break;

    case XSP_WS_CLIENT_LOOP_EVENT_MESSAGE_SENT: {
        ESP_LOGD(TAG, "Event: message sent");
        free(ctx->sending_msg);
        ctx->sending_msg_size = 0;
        ctx->sending_msg = NULL;
        break;
    }
    }
}

// In save-first-message mode, it just saves the first message (and doesn't echo).
static void do_echo(const char* url, bool save_first_msg, int* first_msg_size, void** first_msg) {
    xsp_ws_client_defrag_config_t defrag_config = {
            .max_message_size = 65536,
    };
    xsp_ws_client_config_t client_config = {
            .url = url,
    };
    xsp_ws_client_loop_config_t loop_config = xsp_ws_client_loop_config_default;
    loop_config.max_frame_read_size = 65536;

    do_echo_context_t ctx = {save_first_msg, first_msg_size, first_msg, NULL, 0, NULL, false};

    xsp_ws_client_handle_t client = NULL;
    xsp_ws_client_loop_handle_t loop = NULL;

    ESP_LOGI(TAG, "    Connecting to %s....", url);

    CHECK(!!(ctx.defrag = xsp_ws_client_defrag_init(&defrag_config)));
    CHECK(!!(client = xsp_ws_client_init(&client_config)));
    CHECK(!!(loop = xsp_ws_client_loop_init(&loop_config, client, &do_echo_event_handler, &ctx)));

    CHECK_ERROR(xsp_ws_client_open(client));
    CHECK_ERROR(xsp_ws_client_loop_run(loop));
    CHECK_ERROR(xsp_ws_client_close(client));

    CHECK_ERROR(xsp_ws_client_loop_cleanup(loop));
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
