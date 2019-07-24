// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

// Loop events example.

#include <stddef.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xsp_loop.h"

static const char TAG[] = "MAIN";

typedef struct {
//FIXME
} loop_context_t;

static void on_loop_start(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGI(TAG, "Event: started");
}

static void on_loop_stop(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGI(TAG, "Event: stopped");
}

static void on_loop_idle(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGD(TAG, "Event: idle");  // This is super noisy.
}

static void loop_events_example_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting example");

    esp_err_t err;
    loop_context_t ctx = {};
    xsp_loop_event_handler_t loop_evt_handler = {&on_loop_start, &on_loop_stop, &on_loop_idle,
                                                 &ctx};
    xsp_loop_handle_t loop = NULL;

    loop = xsp_loop_init(NULL, &loop_evt_handler);
    if (!loop) {
        ESP_LOGE(TAG, "Failed to initialize loop");
        goto done;
    }

    err = xsp_loop_run(loop);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to run loop: %s", esp_err_to_name(err));
        goto done;
    }

    ESP_LOGI(TAG, "Yay!");

done:
    if (loop) {
        err = xsp_loop_cleanup(loop);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to clean up loop: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Finished example");

    vTaskDelete(NULL);
}

void app_main(void) {
    xTaskCreate(&loop_events_example_task, "loop_events_example_task", 8192, NULL, 5, NULL);
}
