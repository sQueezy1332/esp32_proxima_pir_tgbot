#pragma once
#pragma GCC diagnostic ignored "-fpermissive"
#define _USE_LONG_TIME_T
#define _USE_32BIT_TIME_T
//#define USE_ESP_IDF_LOG
//#define DEBUG_ENABLE
#include <MAIN.h>
#include <fastbot2.h>
#include <GyverIO.h>
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include <ESPAsyncWebServer.h>
#include "esp_wifi.h"
//#include "time.h"
#ifndef CONFIG_BT_BLE_50_FEATURES_SUPPORTED
#warning "Not compatible hardware"
#define NO_BLE
#endif
#include "OTAserver.h"
#include "credits.h"
//#include "BLE_api.h"

#define ESP32C3_LUATOS
//#define NO_BLE
#ifdef CONFIG_IDF_TARGET_ESP32C3
#define PIN_BUTTON 9
#define PIN_RELAY 8
#if defined ESP32C3_LUATOS
#define PIN_LINE 5
#define PIN_PULLUP 4
#define PIN_LED_D5 12
#define PIN_LED 13
#define LED_ON	HIGH
#define LED_OFF LOW
#else
#define PIN_LINE 4
#define PIN_PULLUP 3
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
#define QUEUE_ITEM_SIZE (sizeof(tgMsg_t))
#define QUEUE_LEN 32

#define lineRead gio::read(PIN_LINE)
#define dWrite(pin, val) gio::write(pin, val)
#define dRead(pin) gio::read(pin)
#define uS esp_timer_get_time()
#define Delay(x) vTaskDelay(pdMS_TO_TICKS(x))
#define DelayUs(x) ets_delay_us(x)
#define TIMER_RECONNECT	60 * 60 * 1000000ul
#define TIMER_SABOTAGE	2500'000
#define TIMER_CHECK		1'000000
#define TIMER_RESEND	60 * 1000000ul
typedef uint32_t _time_t;
using fb::Message, fb::Fetcher;

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
	PANIC,
} stat_t;

typedef struct /*__attribute__((packed))*/ {
	stat_t status;
	byte counter;
	uint16_t delta;
} tgMsg_t;

StackType_t xMainStack[MAIN_TASK_STACK_SIZE], xSendStack[SEND_TASK_STACK_SIZE];
StaticTask_t xMainTaskBuffer, xSendTaskBuffer;
TaskHandle_t loopTaskHandle, sendTaskHandle;	//task
uint8_t QueueMsgStorage[QUEUE_LEN * QUEUE_ITEM_SIZE];
StaticQueue_t pxStaticQueue;
QueueHandle_t QueueMsgHandle;	//queue
//StaticSemaphore_t xMutexBuffer;
//SemaphoreHandle_t mutex; // mutex

stat_t Flag = ok;
__attribute__((unused)) stat_t last_state = ok;
volatile uint64_t last_interrupt = 0xFFFFFF;
volatile uint32_t interrupt_delta = 0;
_time_t timestamp_unix;
uint64_t time_sync_unix;
network_event_handle_t event_id = 0;
gptimer_handle_t timer_sab = nullptr;
String ssid, pass, _login, _password;
AsyncWebServer server(80);
FastBot2 bot(BOT_TOKEN);
FastBot2 bot_upd(BOT_TOKEN);
#ifndef NO_BLE
byte* ble_data = nullptr;
byte ble_data_size = 0;
#endif
void mainTask(void*);
void sendTask(void*);
static void IRAM_ATTR isr_handler(/*void**/);
static bool IRAM_ATTR sabotage_check(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*);
void read_credentials();
void time_sync(uint32_t wait_sec = 10);
bool readFile(cch* path, String& Content);
bool writeFile(cch* path, const String& Content);
bool appendFile(cch* path, _time_t value);
bool deleteFile(cch* path);
void onWiFiConnected(arduino_event_id_t event);
void get_task_list(String& str);
String get_info(bool ver = false);
void wifi_server_init();
bool wifi_sta_init(uint32_t wait_sec = 5);
void onConfigRequest(AsyncWebServerRequest* request);
esp_err_t ble_advertising(cbyte* ble_data, cbyte ble_data_length, uint32_t time_ms = 500);
bool send_alarm_time(Message&& msg, bool no_file = 1);
void updateHandler(fb::Update& u);
void handleMessage(fb::Update& u);
void handleDocument(fb::Update& u);
void otaBegin(fb::Update& u, bool (Fetcher::*)());
void create_hex_string(String& str, cbyte* const& buf, cbyte data_size);
bool strtoB(const String& str, byte sub, byte*& buf, byte& data_len);
void alarm_on() { /*dWrite(PIN_PULLUP, 1);*/enableInterrupt(PIN_LINE); timer_restart(timer_sab); timer_start(timer_sab); };
void alarm_off() { /*dWrite(PIN_PULLUP, 0);*/disableInterrupt(PIN_LINE); timer_stop(timer_sab); };
void resumeTask(stat_t st) { Flag = st; xTaskAbortDelay(loopTaskHandle);/*vTaskResume(mainTaskHandle);*/ };
bool auth_handler(AsyncWebServerRequest*& request) {
	if (*_login.c_str()) {
		if (!request->authenticate(_login.c_str(), _password.c_str())) {
			request->requestAuthentication();
			return false;
		}
	}
	return true;
}

void ota_progress(size_t progress, size_t size) {
	static auto ota_timestamp = uS; auto time = uS;
	if (progress == 0) DEBUGF("OTA overall size bytes: %u\n", size);
	if (time - ota_timestamp > 500000) {
		ota_timestamp = time;
		DEBUG("OTA Progress bytes: ");
		DEBUGLN(progress);
	}
}

template<byte PIN>
struct AutoLed {
	AutoLed() { dWrite(PIN, LED_ON); };
	~AutoLed() { dWrite(PIN, LED_OFF); };
	AutoLed(const AutoLed&) = delete;
	AutoLed& operator=(const AutoLed&) = delete;
};

bool verifyRollbackLater() { return true; };

esp_err_t nvsGet(nvs_handle_t handle, cch* key, nvs_type_t type, uint64_t& result, void* buf, size_t* size = nullptr) {
	esp_err_t ret = ESP_FAIL; size_t required_size;
	switch (type) {
	case NVS_TYPE_U8:ret = nvs_get_u8(handle, key, (uint8_t*)&result); break;
	case NVS_TYPE_I8:ret = nvs_get_i8(handle, key, (int8_t*)&result); break;
	case NVS_TYPE_U16:ret = nvs_get_u16(handle, key, (uint16_t*)&result); break;
	case NVS_TYPE_I16:ret = nvs_get_i16(handle, key, (int16_t*)&result); break;
	case NVS_TYPE_U32:ret = nvs_get_u32(handle, key, (uint32_t*)&result); break;
	case NVS_TYPE_I32:ret = nvs_get_i32(handle, key, (int32_t*)&result); break;
	case NVS_TYPE_U64:ret = nvs_get_u64(handle, key, &result); break;
	case NVS_TYPE_I64:ret = nvs_get_i64(handle, key, (int64_t*)&result); break;
	case NVS_TYPE_STR:ret = -2; 
		nvs_get_str(handle, key, NULL, &required_size);
		buf = malloc(required_size); if (buf == NULL) return -1; 
		{ nvs_get_str(handle, key, (char*)buf, &required_size); }
		if (size) *size = required_size;
		break;
	case NVS_TYPE_BLOB: ret = -3; 
		nvs_get_blob(handle, key, NULL, &required_size);
		buf = malloc(required_size); if (buf == NULL) return -1; 
		nvs_get_blob(handle, key, buf, &required_size);
		if(size)*size = required_size;
		break;
	}
	return ret;
}

void nvs_func() {
	std::vector<const esp_partition_t*> partArr; nvs_stats_t nvs_stats{}; nvs_handle_t nvs;
	nvs_iterator_t it = NULL; nvs_entry_info_t entry; esp_err_t err; 
	auto i = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
	while (i != NULL) {
		partArr.emplace_back(esp_partition_get(i));
		i = esp_partition_next(i);
	}log_i("partition count: %u", partArr.size());
	for (auto& var : partArr) { DEBUGF("Partition label %s, size %u, address 0x%X\n", var->label, var->size, var->address); }
	esp_partition_iterator_release(i);

	nvs_get_stats(NULL, &nvs_stats);
	DEBUGF("UsedEntries = (%lu), FreeEntries = (%lu), AvailableEntries = (%lu), AllEntries = (%lu), Namespaces = (%lu)\n",
		nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.available_entries, nvs_stats.total_entries, nvs_stats.namespace_count);

	err = nvs_entry_find("nvs",NULL, NVS_TYPE_ANY, &it);
	while (err == ESP_OK) {
		nvs_entry_info(it, &entry); // Can omit error check if parameters are guaranteed to be non-NULL
		DEBUGF("space '%s'\tkey '%s'\ttype '%d'", entry.namespace_name, entry.key, entry.type);
		err = nvs_open(entry.namespace_name, NVS_READONLY, &nvs);
		if (err != ESP_OK) { log_e("Error (%s) opening NVS handle!", esp_err_to_name(err)); }
		else {
			std::auto_ptr<void*> str; uint64_t result = 0; size_t _size;
			switch (nvsGet(nvs, entry.key, entry.type, result, str.get(),&_size)) {
			case ESP_OK: DEBUGF("\tData = %llu\n", result); break;
			case -2: DEBUGF("\nStr: %s\n", (char*)str.get()); break;
			case -3: DEBUGF("\nBlob (size %u): ", _size);
				for (size_t i = 0; i < _size; i++) { DEBUGF("%02X ", (char*)(str.get() + i)); }; //
				DEBUGLN(); break;
			default:break;
			}
		}
		err = nvs_entry_next(&it);
		nvs_close(nvs);
	}
	nvs_release_iterator(it);
}


static void IRAM_ATTR interrupt_handler_s() {
	static byte counter = 0; static int last_alarm_delta = -1;
	uint64_t time = uS;
	if (lineRead) return;
	uint32_t delta = time - last_interrupt;
	timer_restart(timer_sab);
	last_interrupt = time; interrupt_delta = delta;
	if (delta < 2400000 /*&& delta > 100000*/) {
		last_alarm_delta = delta; ++counter; isr_log_d("%u", counter);
		if (counter < 10) return;
	}
	else if (counter == 0) return;
	else if (counter != 1) { counter = 0; return; } //2 or 3
#ifdef DEBUG_ENABLE
	if (last_alarm_delta == ok && counter == 1) {
		tgMsg_t tmp = { .status = ok,.counter = 0, .delta = (uint16_t)(delta / 1000) };
		xQueueSendFromISR(QueueMsgHandle, &tmp, nullptr);
		last_alarm_delta = -1; counter = 0; return;
	}
#endif
	tgMsg_t tmp = { .status = ALARM,.counter = counter, .delta = (uint16_t)(last_alarm_delta / 1000) };
	xQueueSendFromISR(QueueMsgHandle, &tmp, nullptr);//sizeof(tgMsg_t)
#ifdef DEBUG_ENABLE
	last_alarm_delta = ok;
#endif
}

//void ble_advertising() {
//	BLEMultiAdvertising advert(1);
//	BLEDevice::init("");
//	advert.setAdvertisingParams(3, &ext_adv_params_coded);
//	advert.setDuration(3);
//	advert.setScanRspData(3, sizeof(raw_scan_rsp_data_coded), &raw_scan_rsp_data_coded[0]);
//	advert.setInstanceAddress(3, addr_coded);
//	auto pBLEScan = BLEDevice::getScan();  //create new scan
//	pBLEScan->setExtendedScanCallback(new MyBLEExtAdvertisingCallbacks());
//	pBLEScan->setExtScanParams();         // use with pre-defined/default values, overloaded function allows to pass parameters
//	delay(1000);                          // it is just for simplicity this example, to let ble stack to set extended scan params
//	pBLEScan->startExtScan(100, 3);  // scan duration in n * 10ms, period - repeat after n seconds (period >= duration)
//}

void IRAM_ATTR timebench() {
	uint64_t start, end; uint64_t count = 0; //gptimer_get_captured_count(timer_sab, &count);
	ENTER_CRITICAL()
		start = uS;
	//gptimer_get_raw_count(timer_sab, &count);
	end = uS;
	EXIT_CRITICAL()
		log_d("delta = %llu", end - start);
}

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
