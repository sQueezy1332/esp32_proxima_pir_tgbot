#pragma once
#ifndef H_ESP_MAIN_
#define H_ESP_MAIN_
//#define DEBUG_ENABLE
//#define NO_GLOBAL_INSTANCES
#define NO_GLOBAL_SERIAL
#include <Arduino.h>
//#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"

#if defined CONFIG_AUTOSTART_ARDUINO
#pragma message "CONFIG_AUTOSTART_ARDUINO"
#endif
#if defined(CONFIG_BT_ENABLED) && SOC_BT_SUPPORTED
#include "esp_bt.h" 
#if CONFIG_IDF_TARGET_ESP32
__weak_symbol bool  btInUse() __weak_symbol { return false; } //overwritten in esp32-hal-bt.c
#else
/*extern */__weak_symbol bool btInUse() { return true; }
#endif
#endif

#if defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED) //CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=y
#define ARDUINO_USB_CDC_ON_BOOT 1
#define ARDUINO_USB_MODE 1
#endif

#if (ARDUINO_USB_CDC_ON_BOOT | ARDUINO_USB_MSC_ON_BOOT | ARDUINO_USB_DFU_ON_BOOT) && !ARDUINO_USB_MODE
#include "USB.h"
#if ARDUINO_USB_MSC_ON_BOOT
#include "FirmwareMSC.h"
#endif
#endif

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
#include "chip-debug-report.h" 
#define DEBUG_ENABLE
#endif
#ifdef DEBUG_ENABLE
#pragma message "DEBUG_ENABLE"
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE //Serial used from Native_USB_CDC | HW_CDC_JTAG        
HWCDC HWCDCSerial; // Hardware CDC mode
#define Serial HWCDCSerial // Arduino Serial is the HW JTAG CDC device
#elif ARDUINO_USB_MODE// !ARDUINO_USB_MODE -- Native USB Mode
USBCDC USBSerial(0); // Arduino Serial is the Native USB CDC device
#define Serial USBSerial
#else   // !ARDUINO_USB_CDC_ON_BOOT -- Serial is used from UART0
extern HardwareSerial Serial0;
#define Serial Serial0
#endif  // ARDUINO_USB_CDC_ON_BOOT
#define SerialBegin(x)  Serial.begin(x); vTaskDelay(1)
#define DEBUG(x, ...) Serial.print(x, ##__VA_ARGS__)
#define DEBUGLN(x, ...) Serial.println(x, ##__VA_ARGS__)
#define DEBUGF(x, ...) Serial.printf(x , ##__VA_ARGS__)
#define CHECK_(x) ESP_ERROR_CHECK_WITHOUT_ABORT(x)
#define CHECK_RET(x) do {esp_err_t ret = (x);\
        if (unlikely(ret != ESP_OK)) { log_e(" 0x%X\t(%s)", ret,  esp_err_to_name(ret)); return;} }while(0)//ESP_RETURN_VOID_ON_ERROR(%s)", err, 
#else
#define DEBUG(x, ...)
#define DEBUGLN(x, ...) 
#define DEBUGF(x, ...)
#define SerialBegin(x)
#define CHECK_(x) (void)(x)
#define CHECK_RET(x) do {esp_err_t ret = (x); if (unlikely(ret != ESP_OK)) {return;} }while(0)
#endif // DEBUG_ENABLE
#define uS esp_timer_get_time()
#define delayms(x) vTaskDelay((x) / portTICK_PERIOD_MS)
#define delayUntil(prev, tmr) vTaskDelayUntil((prev),pdMS_TO_TICKS(tmr))
#define SEC (1000000)
#define ENTER_CRITICAL() {portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;portENTER_CRITICAL(&mux);
#define EXIT_CRITICAL() portEXIT_CRITICAL(&mux);}

typedef const char cch; typedef const uint8_t cbyte; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;

#ifdef CONFIG_APP_ROLLBACK_ENABLE
esp_ota_img_states_t img_state(bool valid = false);
__weak_symbol bool verifyRollbackLater();
#endif 

void nvs_init();
void main_init();
class nvsApi {
private:
	nvs_handle_t _handle = 0;
public:
	nvsApi() {};
	nvsApi(const nvsApi &obj) = delete;
	nvsApi& operator=(const nvsApi&) = delete;
	nvsApi(nvsApi &obj) : _handle(obj._handle) { obj._handle = 0; ESP_LOGD("NVS", "ctor copy"); };
	nvsApi(nvsApi &&obj) : _handle(obj._handle) { obj._handle = 0; ESP_LOGD("NVS", "ctor move"); };
	nvsApi(const char* space, nvs_open_mode_t mode) { begin(space, mode); };
	esp_err_t begin(const char* space, nvs_open_mode_t mode) {
		if(_handle) return 0xDADADA;
		esp_err_t ret = nvs_open(space, mode, &_handle);
		ESP_ERROR_CHECK_WITHOUT_ABORT(ret);
		return ret;
	};
	void close() { if(_handle) { nvs_close(_handle); } };
	~nvsApi() { close(); ESP_LOGD("NVS", "~_handle = %lu", _handle); };
	operator nvs_handle_t() const { return _handle; };
};

/*void pinMode(uint8_t pin, uint8_t mode);
 
 void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
void attachInterruptArg(uint8_t pin, voidFuncPtrArg userFunc, void* arg, int intr_type);
void attachInterrupt(uint8_t pin, voidFuncPtr handler, int mode);
void detachInterrupt(uint8_t pin); */
//xTaskCreateUniversal(loopTask, "loopTask", getArduinoLoopTaskStackSize(), NULL, 1, &loopTaskHandle, ARDUINO_RUNNING_CORE);
#endif //H_ESP_MAIN_