// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

// Loop events example.

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xsp_eventfd.h"
#include "xsp_loop.h"
#include "xsp_loop_events.h"

static const char TAG[] = "MAIN";

typedef struct {
    int n;
} loop_context_t;

typedef void (*my_event_func_t)(xsp_loop_events_handle_t loop_events, loop_context_t* ctx);

static void on_loop_start(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGI(TAG, "Event: started");
}

static void on_loop_stop(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGI(TAG, "Event: stopped");
}

static void on_loop_idle(xsp_loop_handle_t loop, void* raw_ctx) {
    ESP_LOGD(TAG, "Event: idle");  // This is super noisy.
}

static void on_loop_events_event(xsp_loop_events_handle_t loop_events, void* ctx, void* data) {
    ESP_LOGI(TAG, "Loop events event");

    my_event_func_t f;
    memcpy(&f, data, sizeof(my_event_func_t));
    f(loop_events, (loop_context_t*)ctx);
}

static void my_nth_event(xsp_loop_events_handle_t loop_events, loop_context_t* ctx) {
    ctx->n++;

    ESP_LOGI(TAG, "My %d-th event", ctx->n);

    if (ctx->n < 10) {
        my_event_func_t f = &my_nth_event;
        esp_err_t err = xsp_loop_events_post_event(loop_events, &f);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to post event: %s", esp_err_to_name(err));

            err = xsp_loop_stop(xsp_loop_events_get_loop(loop_events));
            if (err != ESP_OK)
                ESP_LOGE(TAG, "Failed to stop loop: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGI(TAG, "Stopping loop");
        esp_err_t err = xsp_loop_stop(xsp_loop_events_get_loop(loop_events));
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to stop loop: %s", esp_err_to_name(err));
    }
}

static void my_first_event(xsp_loop_events_handle_t loop_events, loop_context_t* ctx) {
    ctx->n++;

    ESP_LOGI(TAG, "My first event");

    my_event_func_t f = &my_nth_event;
    esp_err_t err = xsp_loop_events_post_event(loop_events, &f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post event: %s", esp_err_to_name(err));

        err = xsp_loop_stop(xsp_loop_events_get_loop(loop_events));
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to stop loop: %s", esp_err_to_name(err));
    }
}

static void loop_events_example_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting example");

    esp_err_t err;
    loop_context_t ctx = {0};
    xsp_loop_event_handler_t loop_evt_handler = {&on_loop_start, &on_loop_stop, &on_loop_idle,
                                                 &ctx};
    xsp_loop_handle_t loop = NULL;
    xsp_loop_events_config_t loop_events_config = {(int)sizeof(my_event_func_t), 4};
    xsp_loop_events_event_handler_t loop_events_evt_handler = {&on_loop_events_event, &ctx};
    xsp_loop_events_handle_t loop_events = NULL;

    loop = xsp_loop_init(NULL, &loop_evt_handler);
    if (!loop) {
        ESP_LOGE(TAG, "Failed to initialize loop");
        goto done;
    }

    loop_events = xsp_loop_events_init(&loop_events_config, &loop_events_evt_handler, loop);
    if (!loop_events) {
        ESP_LOGE(TAG, "Failed to initialize loop events");
        goto done;
    }

    my_event_func_t f = &my_first_event;
    err = xsp_loop_events_post_event(loop_events, &f);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to post event: %s", esp_err_to_name(err));
        goto done;
    }

    err = xsp_loop_run(loop);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to run loop: %s", esp_err_to_name(err));
        goto done;
    }

    ESP_LOGI(TAG, "Yay!");

done:
    if (loop_events) {
        err = xsp_loop_events_cleanup(loop_events);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to clean up loop events: %s", esp_err_to_name(err));
    }

    if (loop) {
        err = xsp_loop_cleanup(loop);
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Failed to clean up loop: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Finished example");

    vTaskDelete(NULL);
}

void app_main(void) {
    xsp_eventfd_register();

    xTaskCreate(&loop_events_example_task, "loop_events_example_task", 8192, NULL, 5, NULL);
}
