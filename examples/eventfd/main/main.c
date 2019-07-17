// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

//FIXME
//#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xsp_eventfd.h"

int g_fd1 = -1;

static void do_sleep(int ms) {
    vTaskDelay((ms + (portTICK_PERIOD_MS - 1)) / portTICK_PERIOD_MS);
}

static void task1(void* pvParameters) {
    printf("[TASK1] Starting\n");

    printf("[TASK1] Reading fd1 ...\n");
    uint64_t value = (uint64_t)-1;
    ssize_t sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK1]   result = %d, value = %llu\n", (int)sz, (unsigned long long)value);

//FIXME

    vTaskDelete(NULL);
}

void app_main(void) {
    printf("[TASK0] Starting\n");

    printf("[TASK0] Registering eventfd ...\n");
    xsp_eventfd_register();

    printf("[TASK0] Creating fd1 ...\n");
    g_fd1 = xsp_eventfd(0, 0);
    printf("[TASK0]   fd1 = %d\n", g_fd1);

    printf("[TASK0] Creating TASK1\n");
    xTaskCreate(&task1, "TASK1", 8192, NULL, 5, NULL);

    do_sleep(100);

    printf("[TASK0] Writing fd1 ...\n");
    uint64_t value = 1234;
    ssize_t sz = write(g_fd1, &value, sizeof(value));
    printf("[TASK0]   result = %d\n", (int)sz);

//FIXME
//    vTaskDelay(10000 / portTICK_PERIOD_MS);
//    esp_restart();
}
