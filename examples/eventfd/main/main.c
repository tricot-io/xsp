// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xsp_eventfd.h"

static int g_fd1 = -1;
static int g_fd2 = -1;

static void do_sleep(int ms) {
    vTaskDelay((ms + (portTICK_PERIOD_MS - 1)) / portTICK_PERIOD_MS);
}

static void task1(void* pvParameters) {
    printf("[TASK1] Started\n");

    printf("[TASK1] Reading from fd1 ...\n");
    uint64_t value = (uint64_t)-1;
    ssize_t sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK1]   read: result=%d, value=%llu\n", (int)sz, (unsigned long long)value);

    printf("[TASK1] Reading from fd1 ...\n");
    value = (uint64_t)-1;
    sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK1]   read: result=%d, value=%llu\n", (int)sz, (unsigned long long)value);

    printf("[TASK1] Terminating\n");
    vTaskDelete(NULL);
}

static void task2(void* pvParameters) {
    printf("[TASK2] Started\n");

    printf("[TASK2] Reading from fd1 ...\n");
    uint64_t value = (uint64_t)-1;
    ssize_t sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK2]   read: result=%d, value=%llu\n", (int)sz, (unsigned long long)value);

    do_sleep(100);

    printf("[TASK2] Reading from fd1 ...\n");
    value = (uint64_t)-1;
    sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK2]   read: result=%d, value=%llu\n", (int)sz, (unsigned long long)value);

    do_sleep(100);

    printf("[TASK2] Reading from fd1 ...\n");
    value = (uint64_t)-1;
    sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK2]   read: result=%d, value=0x%llx\n", (int)sz, (unsigned long long)value);

    printf("[TASK2] Reading from fd1 ...\n");
    value = (uint64_t)-1;
    sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK2]   read: result=%d, value=0x%llx\n", (int)sz, (unsigned long long)value);

    printf("[TASK2] Reading from fd1 ...\n");
    value = (uint64_t)-1;
    sz = read(g_fd1, &value, sizeof(value));
    printf("[TASK2]   read: result=%d, value=0x%llx, errno=%d\n", (int)sz,
           (unsigned long long)value, errno);

    printf("[TASK2] Terminating\n");
    vTaskDelete(NULL);
}

static void task3(void* pvParameters) {
    printf("[TASK3] Started\n");

    printf("[TASK3] Reading from fd2 ...\n");
    uint64_t value = (uint64_t)-1;
    ssize_t sz = read(g_fd2, &value, sizeof(value));
    printf("[TASK3]   read: result=%d, value=%llu\n", (int)sz, (unsigned long long)value);

    printf("[TASK3] Reading from fd2 ...\n");
    value = (uint64_t)-1;
    sz = read(g_fd2, &value, sizeof(value));
    printf("[TASK3]   read: result=%d, value=0x%llx, errno=%d\n", (int)sz,
           (unsigned long long)value, errno);

    do_sleep(2 * 100);

    printf("[TASK3] Reading from fd2 ...\n");
    value = (uint64_t)-1;
    sz = read(g_fd2, &value, sizeof(value));
    printf("[TASK3]   read: result=%d, value=%llu\n", (int)sz, (unsigned long long)value);

    printf("[TASK3] Reading from fd2 ...\n");
    value = (uint64_t)-1;
    sz = read(g_fd2, &value, sizeof(value));
    printf("[TASK3]   read: result=%d, value=0x%llx, errno=%d\n", (int)sz,
           (unsigned long long)value, errno);

    do_sleep(2 * 100);

    printf("[TASK3] Closing fd2 ...\n");
    int result = close(g_fd2);
    printf("[TASK3]   close: result=%d\n", result);

    printf("[TASK3] Terminating\n");
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

    value = 44;
    printf("[TASK0] Writing to fd1 (value=%llu) ...\n", (unsigned long long)value);
    sz = write(g_fd1, &value, sizeof(value));
    printf("[TASK0]   write: result=%d\n", (int)sz);

    value = 55;
    printf("[TASK0] Writing to fd1 (value=%llu) ...\n", (unsigned long long)value);
    sz = write(g_fd1, &value, sizeof(value));
    printf("[TASK0]   write: result=%d\n", (int)sz);

    do_sleep(100);

    value = 1ULL << 63;
    printf("[TASK0] Writing to fd1 (value=%llu) ...\n", (unsigned long long)value);
    sz = write(g_fd1, &value, sizeof(value));
    printf("[TASK0]   write: result=%d\n", (int)sz);

    value = 1ULL << 63;
    printf("[TASK0] Writing to fd1 (value=%llu) ...\n", (unsigned long long)value);
    sz = write(g_fd1, &value, sizeof(value));
    printf("[TASK0]   write: result=%d\n", (int)sz);

    do_sleep(100);

    printf("[TASK0] Creating fd2 (nonblocking) ...\n");
    g_fd2 = xsp_eventfd(123, XSP_EVENTFD_NONBLOCK);
    printf("[TASK0]   fd2=%d\n", g_fd2);

    printf("[TASK0] Creating TASK3\n");
    xTaskCreate(&task3, "TASK3", 8192, NULL, 5, NULL);

    printf("[TASK0] Closing fd1 ...\n");
    int result = close(g_fd1);
    printf("[TASK0]   close: result=%d\n", result);

    do_sleep(100);

    value = 1ULL << 63;
    printf("[TASK0] Writing to fd2 (value=%llu) ...\n", (unsigned long long)value);
    sz = write(g_fd2, &value, sizeof(value));
    printf("[TASK0]   write: result=%d\n", (int)sz);

    value = 1ULL << 63;
    printf("[TASK0] Writing to fd2 (value=%llu) ...\n", (unsigned long long)value);
    sz = write(g_fd2, &value, sizeof(value));
    printf("[TASK0]   write: result=%d, errno=%d\n", (int)sz, errno);

    do_sleep(2 * 100);

    value = 1ULL << 63;
    printf("[TASK0] Writing to fd2 (value=%llu) ...\n", (unsigned long long)value);
    sz = write(g_fd2, &value, sizeof(value));
    printf("[TASK0]   write: result=%d\n", (int)sz);

    do_sleep(10000);

    printf("[TASK0] Restarting\n");
    esp_restart();
}
