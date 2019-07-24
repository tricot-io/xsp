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

//FIXME
//#include "xsp_eventfd.h"

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
        loop_.Run();
    }

    void OnLoopStarted() override { ESP_LOGI(TAG, "Event: started"); }
    void OnLoopStopped() override { ESP_LOGI(TAG, "Event: stopped"); }
    void OnLoopIdle() override { ESP_LOGD(TAG, "Event: idle"); }

private:
    xsp::Loop loop_{this};
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
//FIXME
//    xsp_eventfd_register();

    xTaskCreate(&cxx_loop_example_task, "cxx_loop_example_task", 8192, NULL, 5, NULL);
}
