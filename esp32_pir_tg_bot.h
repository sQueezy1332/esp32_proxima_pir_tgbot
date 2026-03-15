#pragma once
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough" 
#pragma GCC diagnostic ignored "-Woverloaded-virtual" 
//#pragma GCC diagnostic ignored "-Wextra" 
#pragma GCC diagnostic ignored "-Wunused-label"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#define _USE_LONG_TIME_T
					//#define FIRST_BUILD
					#define CONFIG_PROXIMA_PIR
					#define CONFIG_GENERIC_LINE
#define MBEDTLS_DEBUG_C
#define CONFIG_ASYNC_TCP_STACK_SIZE 8192
#define CONFIG_ASYNC_TCP_USE_WDT 0
//#define USE_ESP_IDF_LOG
#define DEBUG_ENABLE 893750 891442
#include "ESP_MAIN.h"
#include "FastBot2.h"
#include <AsyncTCP.h>
#include "SPIFFS.h"
#include "esp_wifi.h"
#include "rom/crc.h"
#include "esp_adc/adc_continuous.h"
#include "esp_sntp.h"
//#include "esp_check.h"
//#include "mbedtls/md.h"
//#include "time.h"
#ifndef CONFIG_SOC_BLE_50_SUPPORTED
//#warning "Not compatible hardware"

#endif
#define NO_BLE
static_assert(sizeof(time_t) == 4);
#include "OTAserver.h"
#include "credentials.h"
//#include "BLE_api.h"
					#define ESP32C3_LUATOS
					#define RELAY
//#define NO_BLE
#ifdef CONFIG_IDF_TARGET_ESP32C3
#define PIN_BUTTON 9
#define PIN_PWR_BUTTON 7
#define PIN_RELAY 5
#define PIN_LINE 4
#define PIN_ADC_LINE ADC_CHANNEL_1
#define PIN_ADC_PULLUP 0
#define PIN_ADC_PULLUP2 3
#define SAMPLE_BUF				64
#define ADC_TASK_FREQ			10
#define BUF_ADC_SIZE			(SAMPLE_BUF * SOC_ADC_DIGI_RESULT_BYTES)
#define RELAY_STATE(x) (!x) //p-channel mosfet
#define DEF_SWITCH_DELAY (1000)
#if defined ESP32C3_LUATOS
//#define PIN_PULLUP 5
#define PIN_LED_D5 13
#define PIN_LED 12	//D4
#define LED_ON	HIGH
#define LED_OFF LOW
#else
//#define PIN_PULLUP 3
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
#define TAG "MAIN"
#define FUN __FUNCTION__
 
#define MAIN_TASK_STACK_SIZE (8 * 1024)
#define SEND_TASK_STACK_SIZE (8 * 1024)
#define QUEUE_ITEM_SIZE (sizeof(tgMsg_t))
#define QUEUE_LEN 16

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#define EXAMPLE_ADC_OUTPUT_TYPE             ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define EXAMPLE_ADC_GET_CHANNEL(p_data)     ((p_data)->type1.channel)
#define EXAMPLE_ADC_GET_DATA(p_data)        ((p_data)->type1.data)
#else
#define ADC_OUTPUT_TYPE             ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define ADC_GET_CHANNEL(p_data)     ((p_data)->type2.channel)
#define ADC_GET_DATA(p_data)        ((p_data)->type2.data)
#endif

#define lineRead digitalRead(PIN_LINE)
#define dWrite(pin, val) digitalWrite(pin, val)
#define dRead(pin) digitalRead(pin)
#define uS esp_timer_get_time()
#define Delay(x) vTaskDelay(pdMS_TO_TICKS(x))
#define DelayUs(x) ets_delay_us(x)
#define TIMER_SABOTAGE	2500'000
#define timer_restart_impl() gptimer_restart(timer_sab) //xTimerResetFromISR(timerSabotage, NULL);
#define timer_start_impl() {gptimer_restart(timer_sab); gptimer_start(timer_sab);}//xTimerStart(timerSabotage, 0);
#define timer_stop_impl() gptimer_stop(timer_sab)//xTimerStop(timerSabotage, 0);
using fb::Message, fb::Fetcher;

typedef enum : uint8_t {
	ok = 0,
	ALARM,
	GERKON_CLOSE,
	GERKON_OPEN,
	LINE_LOW,
	LINE_HIGH,
	RELAY_0,
	RELAY_1,
	RESEND_MSG,
	CHECK_MSG,
	WIFI_DISCONNECT,
	WIFI_RECON,
	WIFI_INIT,
	RESTART,
	PANIC,
} stat_t;

typedef struct { stat_t status; byte counter; uint16_t delta; } tgMsg_t;

typedef struct { byte alarm, proxima, adc_line, relay;} sets_t;
static_assert(sizeof(sets_t) == 4);
typedef struct { String ssid,pass; } auth_t;

StackType_t xMainStack[MAIN_TASK_STACK_SIZE], xSendStack[SEND_TASK_STACK_SIZE];
StaticTask_t xMainTaskBuffer, xSendTaskBuffer;
TaskHandle_t loopTaskHandle, sendTaskHandle, adcTaskHandle;

uint8_t QueueMsgStorage[QUEUE_LEN * QUEUE_ITEM_SIZE];
StaticQueue_t xStaticQueue;
QueueHandle_t QueueMsgHandle;

__unused adc_continuous_handle_t adc_handle = NULL;
__unused uint8_t adc_buf[BUF_ADC_SIZE];
const uint32_t* ptr_curr_adc = NULL;

uint16_t gerkon_open_high = 0;
uint16_t gerkon_close_high = 0;
uint16_t gerkon_close_low = 0;
uint16_t gerkon_button_low = 0;
uint8_t gerkon_percent_drift = ADC_PERCENT_DEF;

uint16_t gerkon_open_def = ADC_OPEN_DEF;
uint16_t gerkon_close_def = ADC_CLOSE_DEF;
uint16_t gerkon_button_def = ADC_BUTTON_DEF;

//StaticTimer_t  xTimerIntrBuffer/* , xTimerSabBuffer */;
esp_timer_handle_t timer_intr, timer_pwr;
gptimer_handle_t timer_sab = NULL;

volatile stat_t Flag = ok;
__attribute__((unused)) stat_t last_state = ok;
volatile uint64_t last_interrupt = 0xFFFFFF;
//uint64_t time_sync_unix;
//time_t timestamp_unix;
__attribute__((unused)) volatile uint32_t interrupt_delta;
network_event_handle_t event_id;
nvs_handle_t nvsHandle = 0;
sets_t sets;
auth_t* Auth = nullptr;
byte* ble_data = nullptr, ble_data_size = 0;
//String pin_pass;
esp_err_t update_error = ESP_OK;
FastBot2 bot(BOT_TOKEN);
FastBot2 botSend(BOT_TOKEN);
#ifdef FIRST_BUILD
#pragma message "FIRST_BUILD"
#endif
AsyncWebServer server(80);

static struct msg_id_s {
	uint32_t id;
	//uint16_t crc;
} msg_id __attribute__((section(".noinit." "1")));

void mainTask(void*);
void sendTask(void*);
void sabotageCallback(TimerHandle_t);
static void adcReadTask(void*);
static void IRAM_ATTR isr_handler(/*void**/);
static bool IRAM_ATTR sabotage_timer(gptimer_handle_t, const gptimer_alarm_event_data_t*, void*);
static bool IRAM_ATTR conv_done_cb(adc_continuous_handle_t, const adc_continuous_evt_data_t *, void *) {
    vTaskNotifyGiveFromISR(adcTaskHandle, NULL); return false;
};
adc_continuous_handle_t continuous_adc_init(adc_continuous_callback_t cb, const adc_channel_t *channel, uint8_t channel_num, uint16_t buf_size);

void read_credentials();
//void time_sync(byte wait_sec = 10);
bool readFile(cch* path, String& Content);
bool writeFile(cch* path, const String& Content);
bool appendFile(cch* path, time_t value);
bool deleteFile(cch* path);
void onWiFiConnected(arduino_event_id_t event);
void get_task_list(String& str);
String get_task_list() { String str; get_task_list(str); return str;}
String get_info(bool ver = false);
void wifi_server_init();
bool wifi_sta_init(uint32_t = 10 * (1000 / STA_INIT_DELAY));
void wifi_ap_init();
void onConfigRequest(AsyncWebServerRequest* request);
esp_err_t ble_advertising(cbyte* ble_data, cbyte ble_data_length, uint32_t time_ms = 500);
bool send_alarm_time(Message&& msg = Message("", CHAT_ID), FastBot2& _bot = botSend , bool no_file = 1);
void updateHandler(fb::Update& u);
void handleMessage(fb::Update& u);
void handleDocument(fb::Update& u);
void otaBegin(fb::Update& u, bool (Fetcher::*)());
bool strtoB(const String& str, byte*& buf, byte & data_size, byte sub = 0, bool heap = true);
void create_hex_string(String& str, cbyte* buf, cbyte data_size);
String create_hex_string(cbyte* buf, cbyte data_size) {
	String str; create_hex_string(str,buf,data_size); return str;
};
uint32_t generate_pin(const char *str, byte name_len, const String& pass);


void nvs_read_sets();
void nvs_write_sets(nvsApi nvs = nvsApi(NVS_WIFI_SPACE, NVS_READWRITE));
void init_adc_values();
void init_sets();
bool update_adc_sets(cch* data,  String & text);
void alarm_on(bool write = true);
void alarm_off(bool write = true);
void resumeTask(stat_t st = CHECK_MSG) { Flag = st; xTaskNotify(loopTaskHandle,0,eNoAction); };
bool auth_handler(AsyncWebServerRequest*& request) {
	if (Auth && !request->authenticate(Auth->ssid.c_str(), Auth->pass.c_str())) {
			request->requestAuthentication();
			return false;
	}
	return true;
}

void ota_progress(size_t progress, size_t size) {
	static auto ota_timestamp = uS; auto time = uS;
	if (progress == 0) {DEBUGF("OTA overall size bytes: %u\n", size);}
	if (time - ota_timestamp > 500000) {
		ota_timestamp = time;
		DEBUG("OTA Progress bytes: "); DEBUGLN(progress);
	}
}

void push_power_sw(uint32_t time = 100 * 1000) {
	if(esp_timer_start_once(timer_pwr, time) == ESP_OK)
	{ dWrite(PIN_PWR_BUTTON, 0); }
}	

bool toggle_relay_state() {
		const bool new_state = !dRead(PIN_RELAY); sets.relay = RELAY_STATE(new_state);
		dWrite(PIN_RELAY, new_state);
		return RELAY_STATE(new_state);
} 

/* void pir_reset() {
	gpio_set_drive_capability((gpio_num_t)PIN_LINE, GPIO_DRIVE_CAP_3);  dWrite(PIN_LINE, 0); //OPEN_DRAIN
	xTimerStart(xTimerCreate("", pdMS_TO_TICKS(60 * 1000), pdFALSE, NULL, [](TimerHandle_t xTimer) {
		dWrite(PIN_LINE, 1); xTimerDelete(xTimer, 0);}), 0);
} */

template<byte PIN, bool state = LED_ON>
struct AutoLed {
	AutoLed() { dWrite(PIN, state); };
	~AutoLed() { dWrite(PIN, !state); };
	AutoLed(const AutoLed&) = delete;
	AutoLed& operator=(const AutoLed&) = delete;
};

inline void led_blink() {
#if not defined FIRST_BUILD
	static bool state = false;
	dWrite(PIN_LED_D5, state = !state);
#endif
}

/*void noinit_check() {
	if(crc32_le(0, (uint8_t*)&msg_id.id, 4) != msg_id.crc) {
		msg_id.id = 0;
		msg_id.crc = crc32_le(0, (uint8_t*)&msg_id.id, 4);
	}
}*/


//bool verifyRollbackLater() { return true; };

esp_err_t nvsGet(nvs_handle_t handle, cch* key, nvs_type_t type, uint64_t& result, void*& buf, size_t* size = nullptr) {
	esp_err_t ret = ESP_FAIL; size_t required_size; void* ptr;
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
		ptr = realloc(buf, required_size);
		if (ptr == NULL) return -1; buf = ptr;
		nvs_get_str(handle, key, (char*)buf, &required_size);
		if (size) *size = required_size;
		break;
	case NVS_TYPE_BLOB: ret = -3;
		nvs_get_blob(handle, key, NULL, &required_size);
		ptr = realloc(buf, required_size);
		if (ptr == NULL) return -1; buf = ptr;
		nvs_get_blob(handle, key, buf, &required_size);
		if (size) *size = required_size;
		break;
	default:break;
	}
	return ret;
}

void nvs_test() {
	esp_err_t err; void* buf = NULL; uint64_t result = 0; size_t _size = 0;
	nvs_stats_t nvs_stats{}; nvs_handle_t nvs = 0;
	nvs_iterator_t it = NULL; nvs_entry_info_t entry;
	{ std::vector<const esp_partition_t*> partArr;
	auto i = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
	while (i != NULL) {
		partArr.push_back(esp_partition_get(i));
		i = esp_partition_next(i);
	}log_i("partition count: %u", partArr.size());
	for (__unused auto& var : partArr) { DEBUGF("Partition label %s, size %lu, address 0x%lX\n", var->label, var->size, var->address); }
	esp_partition_iterator_release(i); }

	nvs_get_stats(NULL, &nvs_stats);
	DEBUGF("UsedEntries = (%u), FreeEntries = (%u), AvailableEntries = (%u), AllEntries = (%u), Namespaces = (%u)\n",
		nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.available_entries, nvs_stats.total_entries, nvs_stats.namespace_count);

	err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
	while (err == ESP_OK) {
		nvs_entry_info(it, &entry); // Can omit error check if parameters are guaranteed to be non-NULL
		DEBUGF("space '%s'\tkey '%s'\ttype '%d'", entry.namespace_name, entry.key, entry.type);
		err = nvs_open(entry.namespace_name, NVS_READONLY, &nvs);
		if (err != ESP_OK) { log_e("Error nvs = 0x%X (%s)", err, esp_err_to_name(err)); }
		else {
			switch (nvsGet(nvs, entry.key, entry.type, result, buf, &_size)) {
			case ESP_OK: DEBUGF("\tData = %llu\n", result); break;
			case -2: DEBUGF("\nStr: %s\n", (char*)buf); break;
			case -3: DEBUGF("\nBlob (size %u): ", _size);
				for (size_t i = 0; i < _size; i++) { DEBUGF("%02X ", ((byte*)buf)[i]); }; DEBUGLN(); break;
				/*case -2:case -3: DEBUGF("\nBlob (size %u): ", _size);
					for (size_t i = 0; i < _size; i++) { DEBUG(((char*)buf)[i]); delay(10); }; DEBUGLN(); break;*/
			default:break;
			}
		}
		err = nvs_entry_next(&it);
		nvs_close(nvs);
	}
	free(buf);
	nvs_release_iterator(it);
}

void read_credentials_v() {
	char ssid[36], pass[65]; size_t ssid_s = sizeof(pass), pass_s = sizeof(pass); nvs_handle_t nvs = 0;
	CHECK_VOID(nvs_open(NVS_WIFI_SPACE, NVS_READWRITE, &nvs));
	CHECK_VOID(nvs_get_blob(nvs, NVS_KEY_SSID, ssid, &ssid_s));
	CHECK_VOID(nvs_get_blob(nvs, NVS_KEY_PASS, pass, &pass_s));
	ssid_s = strlen(ssid + 4); pass_s = strlen(pass);
	if (ssid_s < 1 || pass_s < 8) {
		log_w("ssid len %u, pass len %u", ssid_s, pass_s);
		//memcpy(ssid, DEFAULT_SSID, sizeof(DEFAULT_SSID));
		//memcpy(pass, DEFAULT_PASS, sizeof(DEFAULT_PASS));
		CHECK_VOID(nvs_set_blob(nvs, NVS_KEY_SSID, DEFAULT_SSID, sizeof(DEFAULT_SSID)));
		CHECK_VOID(nvs_set_blob(nvs, NVS_KEY_PASS, DEFAULT_PASS, sizeof(DEFAULT_PASS)));
		CHECK_VOID(nvs_commit(nvs));
	}nvs_close(nvs);
	/*|| esp_wifi_set_config(WIFI_IF_STA, &current_conf) != ESP_OK
	if (!readFile(SSID_PATH, ssid) || ssid.length() == 0
		|| !readFile(PASS_PATH, pass) || pass.length() < 8) {
		log_e("\nERROR READ WIFI LOGIN"); //delay(2000);
		ssid = DEFAULT_SSID;
		pass = DEFAULT_PASS;
	} DEBUGLN(ssid); DEBUGLN(pass);*/
}

void nvs_wifi_erase(nvs_handle_t nvs = 0) {
	CHECK_VOID(nvs_open(NVS_WIFI_SPACE, NVS_READWRITE, &nvs));
	CHECK_VOID(nvs_erase_all(nvs));
}

static void IRAM_ATTR interrupt_handler_s() {
	static byte counter = 0; static int last_alarm_delta = -1;
	uint64_t time = uS;
	if (lineRead) return;
	uint32_t delta = time - last_interrupt;
	gptimer_restart(timer_sab);
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

void sabotageCallback(TimerHandle_t xTimer) {
	uint64_t time = uS, last = last_interrupt; uint32_t delta = time - last; tgMsg_t tmp;//if (last_state > ALARM) return;
	last_state = tmp.status = (lineRead ? LINE_HIGH : LINE_LOW);
	tmp.delta = (uint16_t)((delta /= 1000) > __UINT16_MAX__ ? __UINT16_MAX__ : delta); log_d("%u", delta);
	xQueueSend(QueueMsgHandle, &tmp, 0);
}

uint32_t generate_pin(const char *str, byte name_len, const String &pass) {
    //const byte name_len = strlen(str); 
	if(!str || !pass.c_str() ) return 0;
	const byte pass_len = pass.length();
    byte shaResult[32];
    byte payload[name_len + pass_len];
    mbedtls_md_context_t ctx {};
    memcpy(payload, str, name_len);
    memcpy(&payload[name_len], pass.c_str(), pass_len);
    DEBUGLN((char*)payload);DEBUGF("name_len = %u; ""payloadLen = %u\n", name_len, pass_len);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, payload,  name_len + pass_len);
    mbedtls_md_finish(&ctx, shaResult);
    mbedtls_md_free(&ctx);
    DEBUG("Hash: "); for (byte i = 0; i < sizeof(shaResult); i++) { DEBUGF("%02x", shaResult[i]);} DEBUGLN();
    uint32_t crcResult = crc32_le(0, shaResult, sizeof(shaResult));
    DEBUGF("crcResult = %lu\n", crcResult);
    crcResult = 1000 + crcResult % 999000;
    DEBUGF("pin = %lu\n", crcResult);// return 100000 + esp_random() % 900000,
    return crcResult;
}
