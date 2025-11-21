#pragma once
#include "driver/gptimer.h"

#define gptimer_restart(x) gptimer_set_raw_count(x, 0)
esp_err_t timer_alarm(gptimer_handle_t handle, uint64_t value, bool reload = 0, uint64_t count = 0);
gptimer_handle_t timer_init(uint64_t value, gptimer_alarm_cb_t func, bool reload = 0, uint8_t priority = 3);
