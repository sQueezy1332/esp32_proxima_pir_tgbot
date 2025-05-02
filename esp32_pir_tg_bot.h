#pragma once
#pragma GCC diagnostic ignored "-fpermissive"
#define _WANT_USE_LONG_TIME_T
#define _USE_LONG_TIME_T
//#define FB_NO_FILE
#include <MAIN.h>
#include <FastBot2.h>
#include <GyverIO.h>
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include <ESPAsyncWebServer.h>
#include <timer_api.h>
#include "esp_wifi.h"
#include "time.h"
#ifndef CONFIG_BT_BLE_50_FEATURES_SUPPORTED
#warning "Not compatible hardware"
#define NO_BLE
#endif
#include <BLEDevice.h>
#include <BLEAdvertisedDevice.h>

#if CORE_DEBUG_LEVEL
#define DEBUG_ENABLE
#endif
#ifdef DEBUG_ENABLE
#pragma message "debug enable"
#define DEBUG(x) Serial.print(x)
#define DEBUGLN(x) Serial.println(x)
#define DEBUGF(x, ...) Serial.printf(x , ##__VA_ARGS__)
#define CHECK_(x) ESP_ERROR_CHECK_WITHOUT_ABORT(x);
#define BOT_TOKEN ""
#define USER_ID "" 
#define CHAT_ID USER_ID
#define SSID_HIDDEN 0
#else
#define DEBUG(x)
#define DEBUGLN(x) 
#define DEBUGF(x, ...)
#define CHECK_(x) (void)(x);
#define NDEBUG
#define BOT_TOKEN ""
#define USER_ID "" 
#define CHAT_ID "" 
#define SSID_HIDDEN 1
#endif // DEBUG_ENABLE
#define ERR_CHECK(x) ESP_ERROR_CHECK_WITHOUT_ABORT(x)
//#define ESP32C3_LUATOS
#define NO_BLE
#define AP_SSID ""
#define DEFAULT_SSID ""
#define DEFAULT_PASS ""
#define WIFI_CHANNEL 13
#define AP_PASS ""
#define CHANGE_AUTH "/config"
#define BLE_SET "/ble_set"
#define RELAY_ON "/relay_on"
#define RELAY_OFF "relay_off"
#define SSID_PATH "/ssid.txt"
#define PASS_PATH "/pass.txt"
#define ALARM_PATH "/alarm.txt"
#ifdef CONFIG_IDF_TARGET_ESP32C3
#define PIN_LINE 4
#define PIN_PULLUP 5
#define PIN_BUTTON 9
#define PIN_RELAY 8
#if defined ESP32C3_LUATOS
#define PIN_LED_D5 12
#define PIN_LED 13
#define LED_ON	HIGH
#define LED_OFF LOW
#else
#define PIN_LED 8
#define LED_ON	LOW
#define LED_OFF HIGH
#endif
#else
#define PIN_LINE 15
#define PIN_PULLUP 14
#define PIN_BUTTON 0
#define PIN_LED 2
#define PIN_RELAY 4
#define LED_ON	HIGH
#define LED_OFF LOW
#endif

#define MAIN_TASK_STACK_SIZE (8 * 1024)
#define SEND_TASK_STACK_SIZE (8 * 1024)
#define QUEUE_ITEM_SIZE (sizeof(tgMessage_t))
#define QUEUE_SIZE (32 * QUEUE_ITEM_SIZE)

#define lineRead gio::read(PIN_LINE)
#define dWrite(pin, val) gio::write(pin, val)
#define dRead(pin) gio::read(pin)
#define uS esp_timer_get_time()
#define Delay(x) vTaskDelay(pdMS_TO_TICKS(x))
#define DelayUs(x) ets_delay_us(x)
#define TIMER_RECONNECT	60 * 60 * 1000000ul
#define TIMER_SABOTAGE	2500000
#define TIMER_CHECK		1000000
#define TIMER_RESEND	60 * 1000000ul
typedef uint32_t _time_t;
using namespace fb;

typedef enum : uint8_t {
	ok = 0,
	ALARM,
	LINE_HIGH,
	LINE_LOW,
	RESEND_MSG,
	CHECK_MSG,
	WIFI_DISCONNECT,
	WIFI_INIT,
	WIFI_RECON,
	RESTART,
} stat_t;

typedef struct /*__attribute__((packed))*/ {
	stat_t status;
	uint16_t delta;
} tgMessage_t;

StackType_t xMainStack[MAIN_TASK_STACK_SIZE], xSendStack[SEND_TASK_STACK_SIZE];
StaticTask_t xMainTaskBuffer, xSendTaskBuffer;
TaskHandle_t loopTaskHandle,sendTaskHandle;
QueueHandle_t QueueStatHandle;
StaticQueue_t pxStaticQueue;
uint8_t QueueStatStorage[QUEUE_SIZE];

//static volatile stat_t prev_alarm = ok;
static stat_t Flag = ok;
bool alarm_state = 0;
volatile uint64_t last_interrupt = 0;
volatile uint32_t interrupt_delta = 0;
_time_t timestamp_unix;
uint64_t timestamp_sync;
#ifndef NO_BLE
byte* ble_data = nullptr;
byte ble_data_size = 0;
#endif
network_event_handle_t event_id = 0;
gptimer_handle_t timer_sab = nullptr;
String ssid, pass;
AsyncWebServer server(80);
FastBot2 bot;

void mainTask(void*);
void sendTask(void*);
void setup(void*);
static void IRAM_ATTR ISR();
bool IRAM_ATTR sabotage_check(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*);
void read_credentials();
void time_sync(byte wait_sec = 10);
bool readFile(cch* path, String& Content);
bool writeFile(cch* path, const String& Content);
bool appendFile(cch* path, _time_t value);
bool deleteFile(cch* path);
void onWiFiConnected(arduino_event_id_t event);
String get_task_list();
String get_info(bool ver = false);
void wifi_server_init();
bool wifi_sta_init(byte wait_sec = 5);
void onConfigRequest(AsyncWebServerRequest* request);
bool ble_advertising(const byte* ble_data, const byte ble_data_length, uint32_t time_ms = 500);
void send_alarm_time(Value const& chat_id, bool no_file = true);
void tg_send(tgMessage_t&);
void updateHandler(fb::Update& u);
void handleMessage(fb::Update& u);
void handleDocument(fb::Update& u);
void otaBegin(fb::Update& u, bool fw);
String create_hex_string(const byte* const& buf, const byte data_size);
bool strtoB(const String& str, byte sub, byte*& buf, byte& data_len, const byte hexSizeMin = 6);
void byte2hexstr(char* text, const byte* buf, const byte data_size);
void alarm_on() { alarm_state = true;/*enableInterrupt(PIN_LINE);*/ /*timer_restart(tmr_sab);timer_start(tmr_sab);*/ };
void alarm_off() { alarm_state = false;/*disableInterrupt(PIN_LINE);*/  /*timer_stop(tmr_sab);*/ };
void resumeTask(stat_t st = RESEND_MSG) { Flag = st; xTaskAbortDelay(loopTaskHandle);/*vTaskResume(mainTaskHandle);*/ };
bool verifyRollbackLater() { return true; };

void suicide_func() {
	//extern StackType_t* shitstack;
	uint32_t rnd = random(0x3FFAE000, 0x400B8000);
	DEBUGF("address = 0x%X, value = %X\n", rnd, ESP_REG(rnd)); delay(5);
	ESP_REG(rnd) = 0;
	auto lorem = "Lorem ipsum dolor sit amet, consectetur adipiscing elit";
	char* strtmp = (char*)malloc(56); memcpy(strtmp, lorem, 56); strtmp[55] = 0;
	uint32_t addrstr = (uint32_t)strtmp;
	DEBUGF("address strtmp = 0x%X\n", addrstr);
	char* newptrstr = (char*)(addrstr);
	for (size_t i = 0; i < 55; i++) { newptrstr[i] = 'A' + i; }
	DEBUGLN(newptrstr);
	free(newptrstr); DEBUGLN(newptrstr);
	for (size_t i = 0; i < sizeof(xMainStack); i++) {
		DEBUGF("%c", xMainStack[i]); DEBUG(' '); if ((i & 7) == 0) DEBUG('\n');
		xMainStack[i] = 0;
	}
}

void suicide_func2(void*) {
	extern StackType_t* shitstack;
	memset(xMainStack, 0xFF, 8192); dWrite(PIN_LED, LED_ON);
	DEBUGLN("memset");/* free(shitstack);*/ vTaskDelete(NULL); return;
	//Guru Meditation Error : Core  0 panic'ed (Instruction access fault). Exception was unhandled.
	for (size_t i = 8192 / 4; i; --i) {
		DEBUGF("%02X", ((uint32_t*)&shitstack)[i]); DEBUG(' '); if ((i & 7) == 0) DEBUG('\n');
		((uint32_t*)&shitstack)[i] = 0;
	}
	DEBUGLN("LOL"); //***ERROR*** A stack overflow in task esp_timer has been detected.
	vTaskDelete(NULL);
}

//static void timerAlarm(uint64_t value, stat_t flag, gptimer_handle_t& handle) {
//	timer_restart(handle); timer_alarm(value, handle);
//	Flag = flag;
//}
