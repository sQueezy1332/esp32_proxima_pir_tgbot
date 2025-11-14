#pragma once
#include "esp_timer.h"

esp_timer_handle_t esp_timer_init(esp_timer_cb_t cb, esp_timer_dispatch_t type = ESP_TIMER_TASK, bool skip = 0, void* arg = nullptr, const char* name = nullptr);
