// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

#include <stdio.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "verify_cxx_atomic.h"
#include "verify_stdatomic.h"

void app_main(void) {
    verify_stdatomic();
    verify_cxx_atomic();
    printf("DONE\n");

    vTaskDelay(10000 / portTICK_PERIOD_MS);
    esp_restart();
}
