#include "ESP_MAIN.h"

//xTaskCreateUniversal(loopTask, "loopTask", getArduinoLoopTaskStackSize(), NULL, 1, &loopTaskHandle, ARDUINO_RUNNING_CORE);
//#undef CONFIG_AUTOSTART_ARDUINO
#if CONFIG_AUTOSTART_ARDUINO
TaskHandle_t loopTaskHandle = NULL;
#if not defined ARDUINO_LOOP_STACK_SIZE && !defined CONFIG_ARDUINO_LOOP_STACK_SIZE
#define ARDUINO_LOOP_STACK_SIZE 8192
#else
#define ARDUINO_LOOP_STACK_SIZE CONFIG_ARDUINO_LOOP_STACK_SIZE
#endif

#if CONFIG_FREERTOS_UNICORE
void yieldIfNecessary(void) {
	static uint64_t lastYield = 0;
	uint64_t now = millis();
	if ((now - lastYield) > 2000) {
		lastYield = now;
		vTaskDelay(5);  //delay 1 RTOS tick
	}
}
#endif
bool loopTaskWDTEnabled;

__attribute__((weak)) size_t getArduinoLoopTaskStackSize(void) {
  return ARDUINO_LOOP_STACK_SIZE;
}

__attribute__((weak)) bool shouldPrintChipDebugReport(void) {
  return false;
}

// this function can be changed by the sketch using the macro SET_TIME_BEFORE_STARTING_SKETCH_MS(time_ms)
__attribute__((weak)) uint64_t getArduinoSetupWaitTime_ms(void) {
  return 0;
}
//xTaskCreateUniversal(loopTask, "loopTask", getArduinoLoopTaskStackSize(), NULL, 1, &loopTaskHandle, ARDUINO_RUNNING_CORE);
void loopTask(void* pvParameters) {
	delay(getArduinoSetupWaitTime_ms());
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_SERIAL)
	// sets UART0 (default console) RX/TX pins as already configured in boot or as defined in variants/pins_arduino.h
	Serial0.setPins(gpioNumberToDigitalPin(SOC_RX0), gpioNumberToDigitalPin(SOC_TX0));
#endif
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
	printBeforeSetupInfo();
#endif
	setup();
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
	printAfterSetupInfo();
#endif
	for (;;) {
#if CONFIG_FREERTOS_UNICORE
		yieldIfNecessary();
#endif
		if (loopTaskWDTEnabled) {
			esp_task_wdt_reset();
		}
		loop();
		if (serialEventRun) {
			serialEventRun();
		}
	}
}
#endif

