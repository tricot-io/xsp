// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

// C++ loop events example.

#include <stddef.h>

#include <memory>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xsp/loop.h"
#include "xsp/loop_event_handler.h"

namespace {

const char TAG[] = "MAIN";

class CxxLoopExampleApp final : public xsp::LoopEventHandler {
public:
    explicit CxxLoopExampleApp() {}
    ~CxxLoopExampleApp() = default;

    // Copy and move not supported.
    CxxLoopExampleApp(const CxxLoopExampleApp&) = delete;
    CxxLoopExampleApp& operator=(const CxxLoopExampleApp&) = delete;

    void Run() {
        bool success = loop_.PostTask([this]() {
            n_++;
            ESP_LOGI(TAG, "First task");
            loop_.PostTask([this]() { NthTask(); });
        });
        if (!success) {
            ESP_LOGE(TAG, "Failed to post first task");
            return;
        }
        loop_.Run();
    }

    void OnLoopStarted() override { ESP_LOGI(TAG, "Event: started"); }
    void OnLoopStopped() override { ESP_LOGI(TAG, "Event: stopped"); }
    void OnLoopIdle() override { ESP_LOGD(TAG, "Event: idle"); }

private:
    void PostNthTask() {
        if (!loop_.PostTask([this]() { NthTask(); })) {
            ESP_LOGE(TAG, "Failed to post n-th task; stopping");
            loop_.Stop();
        }
    }

    void NthTask() {
        n_++;
        ESP_LOGI(TAG, "%d-th task", n_);

        if (n_ < 10) {
            loop_.PostTask([this]() { NthTask(); });
        } else {
            ESP_LOGI(TAG, "Stopping loop");
            loop_.Stop();
        }
    }

    xsp::Loop loop_{this, 8};
    int n_ = 0;
};

void cxx_loop_example_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting loop example");

    {
        std::unique_ptr<CxxLoopExampleApp> cxx_loop_example_app(new CxxLoopExampleApp());
        cxx_loop_example_app->Run();
    }

    ESP_LOGI(TAG, "Finished loop example");

    vTaskDelete(NULL);
}

}  // namespace

extern "C" void app_main(void) {
    xsp::Loop::InitEventfd();

    xTaskCreate(&cxx_loop_example_task, "cxx_loop_example_task", 8192, NULL, 5, NULL);
}
