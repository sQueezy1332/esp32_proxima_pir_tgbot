#pragma once
#undef CONFIG_AUTOSTART_ARDUINO
#define CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT 1
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "soc/rtc.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_ota_ops.h"
#if defined CONFIG_AUTOSTART_ARDUINO
#include "Arduino.h"
#pragma message "CONFIG_AUTOSTART_ARDUINO"
#endif
#ifdef CONFIG_BT_ENABLED
#include "esp32-hal-bt.h"
#endif  //CONFIG_BT_ENABLED
#if (ARDUINO_USB_CDC_ON_BOOT | ARDUINO_USB_MSC_ON_BOOT | ARDUINO_USB_DFU_ON_BOOT) && !ARDUINO_USB_MODE
#include "USB.h"
#if ARDUINO_USB_MSC_ON_BOOT
#include "FirmwareMSC.h"
#endif
#endif
#include "chip-debug-report.h"


#if ARDUINO_USB_CDC_ON_BOOT || (defined CONFIG_IDF_TARGET_ESP32 && ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR)
#define DEBUG_ENABLE
#endif
//#define DEBUG_ENABLE
#ifdef DEBUG_ENABLE
#define DEBUG(x) Serial.print(x)
#define DEBUGLN(x) Serial.println(x)
#define DEBUGF(x, ...) Serial.printf(x , ##__VA_ARGS__)
#define CHECK_(x) ESP_ERROR_CHECK_WITHOUT_ABORT(x);
#else
#define DEBUG(x)
#define DEBUGLN(x) 
#define DEBUGF(x, ...)
#define NDEBUG
#define CHECK_(x) (void)(x);
#endif // DEBUG_ENABLE
#define uS esp_timer_get_time()
#define delayms(x) vTaskDelay((x) / portTICK_PERIOD_MS)
#define delayUntil(prev, tmr) vTaskDelayUntil((prev),(tmr))
#define SEC(x) ((x)*1000000)
typedef const char cch; typedef const uint8_t cbyte; typedef uint32_t dword; typedef uint64_t qword;

void nvs_init() {
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
		if (partition != NULL) {
			err = esp_partition_erase_range(partition, 0, partition->size);
			if (err != ESP_OK) err = nvs_flash_init();
			else log_e("Failed to format the broken NVS partition!");
		}
		else log_e("Could not find NVS partition");
	}
	if (err) log_e("Failed to initialize NVS! Error: %u", err);
}

#ifdef CONFIG_APP_ROLLBACK_ENABLE
esp_ota_img_states_t img_state(bool valid = true) {
	const esp_partition_t* running = esp_ota_get_running_partition();
	esp_ota_img_states_t ota_state;
	esp_ota_get_state_partition(running, &ota_state);
	if (valid && ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
		esp_ota_mark_app_valid_cancel_rollback();
	}
	return ota_state;
}
__weak_symbol extern bool verifyRollbackLater();
#endif

#undef CONFIG_AUTOSTART_ARDUINO
#ifdef CONFIG_BT_ENABLED
#if CONFIG_IDF_TARGET_ESP32
 bool btInUse()__weak_symbol; //overwritten in esp32-hal-bt.c
 bool btInUse() {return false;}
#else
__weak_symbol extern bool btInUse();//from esp32-hal-bt.c
#endif
#endif 

void main_init() {
	//init proper ref tick value for PLL (uncomment if REF_TICK is different than 1MHz)
//ESP_REG(APB_CTRL_PLL_TICK_CONF_REG) = APB_CLK_FREQ / REF_CLK_FREQ - 1;
#ifdef F_XTAL_MHZ
#if !CONFIG_IDF_TARGET_ESP32S2  // ESP32-S2 does not support rtc_clk_xtal_freq_update
	rtc_clk_xtal_freq_update((rtc_xtal_freq_t)F_XTAL_MHZ);
	rtc_clk_cpu_freq_set_xtal();
#endif
#endif
#ifdef F_CPU
	setCpuFrequencyMhz(F_CPU / 1000000);
#endif
#if ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE || defined DEBUG_ENABLE
	log_d("Serial begin\n"); Serial.begin(115200);
#endif
#if ARDUINO_USB_MSC_ON_BOOT && !ARDUINO_USB_MODE
	MSC_Update.begin();
#endif
#if ARDUINO_USB_DFU_ON_BOOT && !ARDUINO_USB_MODE
	USB.enableDFU();
#endif
#if ARDUINO_USB_ON_BOOT && !ARDUINO_USB_MODE
	USB.begin();
#endif
#if CONFIG_AUTOSTART_ARDUINO
#pragma message "initArduino"
	log_d("initArduino\n")
		initArduino();
#else
	nvs_init();
	//esp_log_level_set("*", CONFIG_LOG_DEFAULT_LEVEL);
#ifdef CONFIG_BT_ENABLED
	//if (!btInUse()) { log_d("bt_mem_release");esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);}
#endif
#ifdef CONFIG_APP_ROLLBACK_ENABLE || CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
	if (!&verifyRollbackLater) { log_d("app_valid"); esp_ota_mark_app_valid_cancel_rollback(); }
#endif
#endif
}
//xTaskCreateUniversal(loopTask, "loopTask", getArduinoLoopTaskStackSize(), NULL, 1, &loopTaskHandle, ARDUINO_RUNNING_CORE);
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
bool loopTaskWDTEnabled = false;
__weak_symbol extern TaskHandle_t loopTaskHandle = NULL;

__weak_symbol size_t getArduinoLoopTaskStackSize(void) {
	return ARDUINO_LOOP_STACK_SIZE;
}

__weak_symbol bool shouldPrintChipDebugReport(void) {
	return false;
}
//xTaskCreateUniversal(loopTask, "loopTask", getArduinoLoopTaskStackSize(), NULL, 1, &loopTaskHandle, ARDUINO_RUNNING_CORE);
void loopTask(void* pvParameters) {
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
