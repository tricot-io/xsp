// Copyright 2019 Tricot Inc.
// Use of this source code is governed by the license in the LICENSE file.

// Button event example.

#include <stddef.h>

#include <memory>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "xsp/loop.h"
#include "xsp/loop_event_handler.h"

#include "sdkconfig.h"

namespace {

const char TAG[] = "MAIN";

class ButtonEventExampleApp final : public xsp::LoopEventHandler {
public:
    explicit ButtonEventExampleApp() {}
    ~ButtonEventExampleApp() = default;

    // Copy and move not supported.
    ButtonEventExampleApp(const ButtonEventExampleApp&) = delete;
    ButtonEventExampleApp& operator=(const ButtonEventExampleApp&) = delete;

    void Run() {
        ESP_ERROR_CHECK(gpio_install_isr_service(0));
        bool success = loop_.PostTask([this]() {
            ESP_LOGI(TAG, "Setting up ISR");
            ESP_ERROR_CHECK(gpio_isr_handler_add((gpio_num_t)CONFIG_BUTTON_GPIO_NUM,
                                                 &ButtonEventExampleApp::OnButtonIsrThunk, this));
            const gpio_config_t kGpioConfig = {
                    (uint64_t)1 << CONFIG_BUTTON_GPIO_NUM,
                    GPIO_MODE_INPUT,
                    CONFIG_BUTTON_ACTIVE ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
                    CONFIG_BUTTON_ACTIVE ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
                    CONFIG_BUTTON_ACTIVE ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE,
            };
            ESP_ERROR_CHECK(gpio_config(&kGpioConfig));
        });
        assert(success);
        loop_.Run();
    }

    void OnLoopStarted() override { ESP_LOGI(TAG, "Event: started"); }
    void OnLoopStopped() override { ESP_LOGI(TAG, "Event: stopped"); }
    void OnLoopIdle() override { ESP_LOGD(TAG, "Event: idle"); }

private:
    static void OnButtonIsrThunk(void* thiz) {
        static_cast<ButtonEventExampleApp*>(thiz)->OnButtonIsr();
    }
    void OnButtonIsr() {
        loop_.PostTask([this]() { ESP_LOGI(TAG, "Button released"); });
    }

    xsp::Loop loop_{this, 8};
};

void button_event_example_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting button event example");

    {
        std::unique_ptr<ButtonEventExampleApp> button_event_example_app(
                new ButtonEventExampleApp());
        button_event_example_app->Run();
    }

    ESP_LOGI(TAG, "Finished button event example");

    vTaskDelete(NULL);
}

}  // namespace

extern "C" void app_main(void) {
    xsp::Loop::InitEventfd();

    xTaskCreate(&button_event_example_task, "button_event_example_task", 8192, NULL, 5, NULL);
}
