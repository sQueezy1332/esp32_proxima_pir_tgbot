#pragma once
#pragma GCC diagnostic ignored "-fpermissive"
#define _WANT_USE_LONG_TIME_T
#define _USE_LONG_TIME_T
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
#endif
#include <BLEDevice.h>
#include <BLEAdvertisedDevice.h>
#define MAIN_TASK_STACK_SIZE (16 * 1024)
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
#else
#define DEBUG(x)
#define DEBUGLN(x) 
#define DEBUGF(x, ...)
#define CHECK_(x) (void)(x);
#define NDEBUG
#define BOT_TOKEN ""
#define USER_ID "" 
#define CHAT_ID "" 
#endif // DEBUG_ENABLE
#define ERR_CHECK(x) ESP_ERROR_CHECK_WITHOUT_ABORT(x)
#define ESP32C3_LUATOS
#define AP_SSID ""
#define DEFAULT_SSID ""
#define DEFAULT_PASS ""
#ifdef CONFIG_IDF_TARGET_ESP32
#define PIN_LINE 15
#define PIN_BUTTON 0
#define PIN_LED 2
#define PIN_RELAY 4
#elif defined CONFIG_IDF_TARGET_ESP32C3
#define PIN_LINE 4
#define PIN_BUTTON 9
#define PIN_RELAY 8
#if defined ESP32C3_LUATOS
#define PIN_LED_D5 13
#define PIN_LED 12
#endif
#else
#define PIN_LED 8
#endif
#if defined ESP32C3_LUATOS
#define LED_ON	HIGH
#define LED_OFF LOW
#elif defined CONFIG_IDF_TARGET_ESP32C3
#define LED_ON	LOW
#define LED_OFF HIGH
#endif

#define WIFI_CHANNEL 13
#define SSID_HIDDEN 1
#define AP_PASS ""
#define CHANGE_AUTH "/config"
#define BLE_SET "/ble_set"
#define RELAY_ON "/relay_on"
#define RELAY_OFF "relay_off"
#define SSID_PATH "/ssid.txt"
#define PASS_PATH "/pass.txt"
#define ALARM_PATH "/alarm.txt"

#define lineRead gio::read(PIN_LINE)
#define dWrite(pin, val) gio::write(pin, val)
#define dRead(pin) gio::read(pin)
#define uS esp_timer_get_time()
#define TIMER_RECONNECT	60 * 60 * 1000000ul
#define TIMER_SABOTAGE	2500000
#define TIMER_CHECK		1000000
#define TIMER_RESEND	60 * 1000000ul
typedef uint32_t _time_t;
using namespace fb;

typedef enum : uint8_t {
	LINE_OK,
	ALARM,
	LINE_HIGH,
	LINE_LOW,
	CHECK_MSG,
	WIFI_DISCONNECT,
	WIFI_INIT,
	WIFI_RECON,
	RESEND_MSG,
	RESTART
} stat_t;

static StackType_t xMainStack[MAIN_TASK_STACK_SIZE];
static StaticTask_t xMainTaskBuffer;
TaskHandle_t mainTaskHandle;

static volatile stat_t status;
stat_t prev_status = LINE_OK, interrupt_flag = CHECK_MSG;
network_event_handle_t event_id = 0;
volatile uint64_t last_interrupt = 0;
volatile uint32_t alarm_delta = 0, interrupt_delta = 0;
_time_t timestamp_unix;
uint64_t timestamp_sync;
size_t timestamps_count;
_time_t* timestamp_arr = nullptr;
byte* ble_data = nullptr;
byte ble_data_size = 0;
gptimer_handle_t tmrSab = nullptr, tmrWifi = nullptr;
String* ssid = nullptr, * pass = nullptr;

AsyncWebServer server(80);
FastBot2 bot;

static void IRAM_ATTR ISR();
bool IRAM_ATTR sabotage_check(gptimer_handle_t tmr, const gptimer_alarm_event_data_t* edata, void* user_ctx);
bool IRAM_ATTR wifi_check(gptimer_handle_t tmr, const gptimer_alarm_event_data_t* edata, void* user_ctx);
static void timerAlarm(uint64_t value, stat_t flag, gptimer_handle_t& handle = tmrWifi);
void read_credentials();
void time_sync(byte wait_sec = 10);
bool readFile(cch* path, String*& Content);
bool writeFile(cch* path, String*& Content);
bool readFile(cch* path, _time_t*& buf, size_t& size);
bool appendFile(cch* path, _time_t value);
bool deleteFile(cch* path);
static String get_info();
void wifi_server_init();
bool wifi_sta_init(byte wait_sec = 5);
void onConfigRequest(AsyncWebServerRequest* request);
void tg_send(cch* text, cch* chat = CHAT_ID);
bool ble_advertising(const byte* ble_data, const byte ble_data_length, uint32_t time_ms = 500);
String create_hex_string(const byte* const& buf, const byte data_size);
bool strtoB(const String& str, byte sub, byte*& buf, byte& data_len, const byte hexSizeMin = 6);
bool send_alarm_time(Value const& chat_id);
void updateHandler(fb::Update& u);
void otaBegin(fb::Update& u, bool fw = true);
void handleDocument(fb::Update& u);
void handleMessage(fb::Update& u);
bool verifyRollbackLater() { return true; };
