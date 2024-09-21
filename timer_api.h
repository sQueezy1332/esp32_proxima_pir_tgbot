#include <driver/gptimer.h>

#define timer_start(x) gptimer_start((x))
#define timer_stop(x) gptimer_stop((x))
#define timer_restart(x) gptimer_set_raw_count((x), 0)

//__attribute__((weak)) extern gptimer_handle_t timer0, timer1;
//__attribute__((weak)) extern auto interrupt_flag;
#ifdef  __cplusplus
extern "C" {
#endif //  __cplusplus
	esp_err_t timer_alarm(uint64_t value, gptimer_handle_t& handle, const bool reload = 1) {
		gptimer_alarm_config_t alarm_cfg{
			.alarm_count = value,
			.reload_count = 0,
			.flags = {.auto_reload_on_alarm = reload} //.flags.auto_reload_on_alarm = reload,
		};
		return gptimer_set_alarm_action(handle, &alarm_cfg);
		//interrupt_flag = flag;
	}
	esp_err_t timer_init(uint64_t value, gptimer_handle_t& handle, gptimer_alarm_cb_t func, const bool start = 1, const byte prio = 1, const bool reload = 1) {
		free(handle); //log_d("timer_start = %u\n", start);
		gptimer_config_t config {
			.clk_src = GPTIMER_CLK_SRC_DEFAULT,
			.direction = GPTIMER_COUNT_UP,
			.resolution_hz = 1000000,
			.intr_priority = prio,
			.flags = {.intr_shared = 1},
		}; //SOC_TIMER_GROUP_TOTAL_TIMERS
		esp_err_t ret = ESP_OK;
		gptimer_event_callbacks_t cbs = { .on_alarm = func };
		if ((ret = gptimer_new_timer(&config, &handle))
			|| (ret = timer_alarm(value, handle, reload))
			|| (ret = gptimer_register_event_callbacks(handle, &cbs, NULL))
			|| (ret = gptimer_enable(handle))) goto exit;
		if (start) ret = gptimer_start(handle);
	exit://log_d("err = %u", ret);
		return ret;
	}
#ifdef  __cplusplus
}
#endif //  __cplusplus


