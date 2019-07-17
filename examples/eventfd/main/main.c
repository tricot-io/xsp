// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xsp_eventfd.h"

int g_fd1 = -1;

static void do_sleep(int ms) {
    vTaskDelay((ms + (portTICK_PERIOD_MS - 1)) / portTICK_PERIOD_MS);
}

static void task1(void* pvParameters) {
    printf("[TASK1] Starting\n");

    printf("[TASK1] Reading from fd1 ...\n");
    uint64_t value = (uint64_t)-1;
    ssize_t sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK1]   read: result=%d, value=%llu\n", (int)sz, (unsigned long long)value);

    printf("[TASK1] Reading from fd1 ...\n");
    value = (uint64_t)-1;
    sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK1]   read: result=%d, value=%llu\n", (int)sz, (unsigned long long)value);

    // TODO(vtl): Moar.

    printf("[TASK1] Terminating\n");
    vTaskDelete(NULL);
}

static void task2(void* pvParameters) {
    printf("[TASK2] Starting\n");

    printf("[TASK2] Reading from fd1 ...\n");
    uint64_t value = (uint64_t)-1;
    ssize_t sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK2]   read: result=%d, value=%llu\n", (int)sz, (unsigned long long)value);

    // TODO(vtl): Moar.

    printf("[TASK2] Terminating\n");
    vTaskDelete(NULL);
}

void app_main(void) {
    printf("[TASK0] Starting\n");

    printf("[TASK0] Registering eventfd ...\n");
    xsp_eventfd_register();

    printf("[TASK0] Creating fd1 ...\n");
    g_fd1 = xsp_eventfd(0, 0);
    printf("[TASK0]   fd1=%d\n", g_fd1);

    printf("[TASK0] Creating TASK1\n");
    xTaskCreate(&task1, "TASK1", 8192, NULL, 5, NULL);

    do_sleep(100);

    uint64_t value = 1234;
    printf("[TASK0] Writing to fd1 (value=%llu) ...\n", (unsigned long long)value);
    ssize_t sz = write(g_fd1, &value, sizeof(value));
    printf("[TASK0]   write: result=%d\n", (int)sz);

    printf("[TASK0] Creating TASK2\n");
    xTaskCreate(&task2, "TASK2", 8192, NULL, 5, NULL);

    do_sleep(100);

    value = 56;
    printf("[TASK0] Writing to fd1 (value=%llu) ...\n", (unsigned long long)value);
    sz = write(g_fd1, &value, sizeof(value));
    printf("[TASK0]   write: result=%d\n", (int)sz);

    value = 78;
    printf("[TASK0] Writing to fd1 (value=%llu) ...\n", (unsigned long long)value);
    sz = write(g_fd1, &value, sizeof(value));
    printf("[TASK0]   write: result=%d\n", (int)sz);

    // TODO(vtl): Moar.

    do_sleep(10000);

    printf("[TASK0] Restarting\n");
    esp_restart();
}
