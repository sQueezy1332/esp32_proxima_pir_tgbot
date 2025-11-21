#include "hwtimer.h"

esp_err_t timer_alarm(gptimer_handle_t handle, uint64_t value, bool reload, uint64_t count) {
		gptimer_alarm_config_t alarm_config{
			.alarm_count = value,
			.reload_count = count,
			.flags = {.auto_reload_on_alarm = reload} //.flags.auto_reload_on_alarm = reload,
		};
		return gptimer_set_alarm_action(handle, &alarm_config);
	}

gptimer_handle_t timer_init(uint64_t value, gptimer_alarm_cb_t func, bool reload, uint8_t priority) {
		esp_err_t ret; gptimer_handle_t handle;
		gptimer_config_t config {
			.clk_src = GPTIMER_CLK_SRC_DEFAULT,
			.direction = GPTIMER_COUNT_UP,
			.resolution_hz = 1000000,
			.intr_priority = priority,
			.flags { .intr_shared = 1, .allow_pd = 0,.backup_before_sleep = 0 },
		}; //SOC_TIMER_GROUP_TOTAL_TIMERS
		gptimer_event_callbacks_t cbs = { .on_alarm = func };
		if ((ret = gptimer_new_timer(&config, &handle)) 
			|| (ret = gptimer_register_event_callbacks(handle, &cbs, NULL))
			|| (ret = gptimer_enable(handle))
			|| (ret = timer_alarm(handle, value, reload))) 
			{ ESP_ERROR_CHECK_WITHOUT_ABORT(ret); return NULL;}
		return handle;
	}

uint64_t timer_read(gptimer_handle_t handle) {
	uint64_t value = 0;
	esp_err_t ret = gptimer_get_raw_count(handle, &value);
	ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
	return value;
}
