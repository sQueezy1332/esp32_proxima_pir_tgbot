#define _WANT_USE_LONG_TIME_T
#include <FastBot2.h>
#include <GyverIO.h>
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include <ESPAsyncWebServer.h>
#include "time.h"
#include "esp_wifi.h"
#include "esp32-hal-timer.h"
//#include "timer_api.h"
//#include <driver/gptimer.h>
//#pragma GCC diagnostic ignored "-fpermissive"
#ifndef CONFIG_BT_BLE_50_FEATURES_SUPPORTED
#warning "Not compatible hardware"
#endif
#include <BLEDevice.h>
#include <BLEAdvertisedDevice.h>

#if ARDUINO_USB_CDC_ON_BOOT
#define DEBUG_ENABLE
#endif
#ifdef DEBUG_ENABLE
#pragma message "debug enable"
#define DEBUG(x) Serial.print(x)
#define DEBUGLN(x) Serial.println(x)
#define DEBUGF(x, ...) Serial.printf(x , ##__VA_ARGS__)
#define BOT_TOKEN ""
#define USER_ID ""//
#define CHAT_ID USER_ID //
#else
#define DEBUG(x)
#define DEBUGLN(x) 
#define DEBUGF(x, ...)
#define NDEBUG
#define BOT_TOKEN ""
#define USER_ID "" 
#define CHAT_ID "" 
#endif // DEBUG_ENABLE
#define ERR_CHECK(x) ESP_ERROR_CHECK_WITHOUT_ABORT(x)
#define ESP32C3_LUATOS
#define AP_SSID CONFIG_IDF_TARGET
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
#define TIMER_RESEND	30 * 1000000ul
using namespace fb;

//typedef uint32_t time_t;

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

volatile stat_t status = LINE_OK;
stat_t prev_status = LINE_OK, interrupt_flag = CHECK_MSG;
volatile uint64_t last_interrupt;
volatile uint32_t interrupt_delta;

time_t timestamp_unix;
uint64_t timestamp_sync;
size_t timestamps_count;
time_t* timestamp_arr = nullptr;

hw_timer_t* timer0 = nullptr, * timer1 = nullptr;

String* ssid = nullptr, * pass = nullptr;

AsyncWebServer server(80);
//WiFiClientSecure client;
FastBot2 bot;

byte* ble_data = nullptr;
byte ble_data_size = 0;

static void IRAM_ATTR ISR() {
	//if (lineRead == LOW) {
	uint64_t time = uS;
	uint32_t delta = time - last_interrupt;
	if (delta < 2400000 && delta > 1000) {
		status = ALARM;
	}
	else if (prev_status == status) status = LINE_OK;
	last_interrupt = time;
	interrupt_delta = delta;
	timerRestart(timer0);
	//} else if (uS - last_interrupt > 2000) status = LINE_LOW;
}

static void IRAM_ATTR sabotage_check() {
	lineRead ? status = LINE_HIGH : status = LINE_LOW;
}

static void IRAM_ATTR wifi_check() {
	if (prev_status == status) status = interrupt_flag;
}

static void timer_alarm(uint64_t value, stat_t flag, hw_timer_t*& handle = timer1) {
	timerRestart(handle); timerAlarm(handle, value, true, 0);
	interrupt_flag = flag;
}

static void timers_init(uint64_t timer0_value, uint64_t timer1_value) {
	//if (timerPtr0 != nullptr || timerPtr1 != nullptr) delete timer0, timer1;
	timer0 = timerBegin(1000000); //timerStop(timer0);
	timer1 = timerBegin(1000000); //timerStop(timer1);
	timerAttachInterrupt(timer0, &sabotage_check);
	timerAttachInterrupt(timer1, &wifi_check);
	timerAlarm(timer0, timer0_value, true, 0);
	timerAlarm(timer1, timer1_value, true, 0);
	//timerRestart(timer0);
}

void read_credentials() {
	if (!readFile(SSID_PATH, ssid) || ssid->length() == 0
		|| !readFile(PASS_PATH, pass) || pass->length() < 8) {
		DEBUGLN("\nERROR READ WIFI LOGIN"); //delay(2000);
		//if (!digitalRead(PIN_BUTTON)) while (status != WIFI_INIT) { taskYIELD(); }else ESP.restart();
		delete ssid, pass;
		ssid = new String(DEFAULT_SSID);
		pass = new String(DEFAULT_PASS);
	} //DEBUGLN(*ssid); DEBUGLN(*pass);
}

void time_sync(byte wait_sec = 10) {
	uint64_t ticker = uS;
	time_t now = 0; time(&now);
	while (now < 1000000000) {
		if (uS - ticker > 1000000) {
			ticker = uS;
			time(&now);
			if (--wait_sec == 0) {
				DEBUGLN("\nTime sync failed!");
				return;
			}
		}
	}
	timestamp_sync = uS;
	timestamp_unix = now; DEBUGLN(timestamp_unix);
}

bool readFile(const char* path, String*& Content) {
	DEBUG("Reading file: "); DEBUGLN(path);
	fs::File file = SPIFFS.open(path, FILE_READ);
	if (!file || file.isDirectory()) {
		DEBUGLN(" failed to open file for reading");
		return false;
	}
	if (!file.available()) return false;
	delete Content;
	Content = new String(file.readStringUntil('\0'));
	return true;
}

bool writeFile(const char* path, String*& Content) {
	DEBUG("Writing file: "); DEBUG(path);
	fs::File file = SPIFFS.open(path, FILE_WRITE);
	if (!file) {
		DEBUGLN(" failed to open file for writing");
		return false;
	}
	if (file.print(*Content)) {
		DEBUGLN(" file written");
		return true;
	}
	else {
		DEBUGLN(" write failed");
		return false;
	}
}

bool readFile(const char* path, time_t*& buf, size_t& size) {
	DEBUG("Reading file: "); DEBUGLN(path);
	fs::File file = SPIFFS.open(path, FILE_READ);
	if (!file || file.isDirectory() || !file.available()) {
		DEBUGLN(" failed to open file for reading");
		return false;
	}
	if (size_t count = file.size() / sizeof(time_t)) {
		delete[] buf; size = count;
		buf = new time_t[count];
		file.read((byte*)buf, count * sizeof(time_t));
		return true;
	}
	return false;
}

bool appendFile(const char* path, time_t value) {
	DEBUG("Append file: "); DEBUG(path);
	fs::File file = SPIFFS.open(path, FILE_APPEND);
	if (!file) {
		DEBUGLN(" failed to open file for writing");
		return false;
	}
	if (file.write((byte*)&value, sizeof(time_t))) {
		DEBUGLN(" file written");
		return true;
	}
	else {
		DEBUGLN(" write failed");
		return false;
	}
}

bool deleteFile(const char* path) {
	DEBUG("Deleting file: "); DEBUG(path);
	if (SPIFFS.remove(path)) {
		DEBUGLN(" file deleted");
		return true;
	}
	else DEBUGLN(" delete failed"); return false;
}

String get_info() {
	uint32_t heap = ESP.getFreeHeap(); uint32_t sec = uS / 1000000; String str; str.reserve(128);
	str += "Connected to: "; str += *ssid; str += "\nLocal IP: "; str += WiFi.localIP().toString(); str += "\nRSSI: "; str += WiFi.RSSI();
	str += "\nFree Heap: "; str += heap; str += "\nUptime: "; str += sec / 3600 / 24;  str += "d "; str += sec / 3600 % 24; str += "h "; str += sec / 60 % 60;
	str += "m "; str += sec % 60; str += 's'; str += "\ninterrupt_delta = ", str += interrupt_delta / 1000; DEBUGLN(str.length());
	DEBUGLN(str); return str;
}

bool wifi_sta_init(byte wait_sec = 5) {
	WiFi.begin(*ssid, *pass); DEBUGLN("Connecting ...");
	uint64_t timestamp = uS + (wait_sec * 1000000ul);
	for (wl_status_t stat = WiFi.status(); stat != WL_CONNECTED; stat = WiFi.status()) {
#ifdef DEBUG_ENABLE
		delay(1000); DEBUG(stat); DEBUG(' ');
#endif // DEBUG_ENABLE
		if (uS > timestamp) return false;
	} DEBUGLN();
	return true;
}

void wifi_server_init() {
#if	WIFI_CHANNEL > 11
	ERR_CHECK(esp_wifi_set_country_code("CN", false));
#endif
	WiFi.softAP(AP_SSID, AP_PASS, WIFI_CHANNEL, SSID_HIDDEN);
	//for (uint64_t timer = uS + 1000000; !(WiFi.getStatusBits() & AP_STARTED_BIT);) { if (uS > timer) ESP.restart(); }
	WiFi.setTxPower(WIFI_POWER_20dBm); DEBUGLN(WiFi.getTxPower()); //WIFI_POWER_20dBm = 80,// 20dBm
	WiFi.bandwidth(WIFI_BW_HT20);
	DEBUGLN("\nAP running"); DEBUGLN(AP_SSID); DEBUGLN(AP_PASS); DEBUG("My IP address: "); DEBUGLN(WiFi.softAPIP());
	// Handle requests for pages that do not exist
	server.onNotFound([](AsyncWebServerRequest* request) {
		DEBUGLN("[" + request->client()->remoteIP().toString() + "] HTTP GET request of " + request->url());
		request->send(404, "text/plain", "Not found");
		});
	server.on("/connect", HTTP_GET, [](AsyncWebServerRequest* request) {
		String str = "Connecting to:\n"; "SSID = ["; str += *ssid; str += "]\n"; str += "PASS = ["; str += *pass; str += "]\n";
		request->send(200, "text/plain", str);
		status = WIFI_INIT;
		});
	server.on("/disconnect", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Disconnecting...");
		timer_alarm(TIMER_RECONNECT, WIFI_DISCONNECT); WiFi.disconnect();
		});
	server.on("/restart", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Esp restarting...");
		dWrite(PIN_LED, LED_ON);
		ESP.restart();
		});
#if defined RELAY
	server.on(RELAY_ON, HTTP_GET, [](AsyncWebServerRequest* request) {
		dWrite(PIN_RELAY, HIGH);
		request->send(200, "text/plain", "RELAY ON");
		});
	server.on(RELAY_OFF, HTTP_GET, [](AsyncWebServerRequest* request) {
		dWrite(PIN_RELAY, LOW);
		request->send(200, "text/plain", "RELAY OFF");
		});
#endif
	server.on(CHANGE_AUTH, HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(SPIFFS, "/config.html", "text/html");
		});
	server.on(CHANGE_AUTH, HTTP_POST, onConfigRequest);
	//ElegantOTA.onEnd(onOTAEnd);
	//ElegantOTA.begin(&server);
	server.begin(); // Start server
}

void onConfigRequest(AsyncWebServerRequest* request) {
	AsyncWebParameter* pSSID = request->getParam(0), * pPASS = request->getParam(1);
	bool wrongSSID = pSSID->value().length() == 0, wrongPASS = pPASS->value().length() < 8;
	if (wrongSSID && wrongPASS) {
		request->send(200, "text/plain", "WRONG INPUT");
		return;
	}
	String log;
	if (!wrongSSID) {
		delete ssid; ssid = new String(pSSID->value()); //pSSID->value().length()
		if (!writeFile(SSID_PATH, ssid)) {
			log += "ERROR WRITE "; log += SSID_PATH; log += '\n';
		}
	}
	if (!wrongPASS) {
		delete pass; pass = new String(pPASS->value());
		if (!writeFile(PASS_PATH, pass)) {
			log += "ERROR WRITE ";  log += PASS_PATH; log += '\n';
		}
	}/////DEBUGf("POST[%s]: %s\n", pSSID->name().c_str(), pSSID->value().c_str(), pPASS->name().c_str(), pPASS->value().c_str());
	log += "SSID = ["; log += *ssid; log += "]\n"; log += "PASS = ["; log += *pass; log += "]\n";
	log += "Done. Connecting with new credentials"; DEBUGLN(log);
	request->send(200, "text/plain", log);
	status = WIFI_INIT;
}

void tg_send(String msg, const char* chat = CHAT_ID) {
	auto _status = status; dWrite(PIN_LED, LED_ON);
	if (_status == ALARM) { msg += '\t'; msg += interrupt_delta / 1000; }
	if ((WiFi.isConnected() || wifi_sta_init()) && !bot.sendMessage(Message(msg, chat))) {
		if (_status != LINE_OK) {
			appendFile(ALARM_PATH, timestamp_unix + ((uS - timestamp_sync) / 1000000));
			timer_alarm(TIMER_RESEND, RESEND_MSG);
		}
	} prev_status = _status;
	dWrite(PIN_LED, LED_OFF);
}
__attribute__((weak)) bool ble_advertising(const byte* ble_data, const byte ble_data_length, uint32_t time_ms = 500) {
	if (ble_data == nullptr || ble_data_length == 0) return false; DEBUGLN(ESP.getFreeHeap());
	return true;
}

__attribute__((unused)) bool validate_hex_string(fb::Update& u, byte sub, byte*& data, byte data_len) {
	uint32_t str_len = u.message().text().length(), hex_len = (str_len + 1 - sub) / 3; DEBUGLN(hex_len);
	if (hex_len < 6 || hex_len > 251 || u.message().text()[str_len] == ' ') return false;
	for (uint32_t i = sub - sizeof('\0'); i < str_len; i += 3)
		if (u.message().text()[i] != ' ') return false;
	delete[] data;
	data = new byte[data_len = hex_len]{};
	for (uint32_t offset = sub, i = 0; i < hex_len; offset += 3, i++) {
		data[i] = strtoul(&u.message().text()._str[offset], NULL, HEX);
	}return true;
}

String create_hex_string(const byte* const& buf, const byte data_size) {
	String str("", data_size * 3 - 1);
	char* offset = &str[0]; uint32_t i = 0;
	for (; i < data_size - 1; i++, offset += 3) {
		sprintf(offset, "%02X ", buf[i]);
	}
	sprintf(offset, "%02X", buf[i]); //DEBUGLN(str.length());
	return str;
}

bool strtoB(const String& str, byte sub, byte*& buf, byte& data_len) {
	unsigned str_len = str.length() - sub, hex_len = (str_len + 1) / 2; //DEBUGLN(hex_len);
	if(hex_len > 255) return false;
	const char* ptr = str.c_str() + sub;
	free(buf);
	buf = (byte*)calloc(hex_len, sizeof(byte));
	byte i = 0;
	for (byte ready = 0, result = 0; *ptr/* && i < hex_len*/; ++ptr) {
		switch (*ptr) {
		case'0'... '9':
			if (result & 0xf) result <<= 4;
			result |= (*ptr ^ 0x30); break;
		case 'a'...'f':
			if (result & 0xf) result <<= 4;
			result |= *ptr - 87; break;
		case 'A'... 'F':
			if (result & 0xf) result <<= 4;
			result |= *ptr - 55; break;
		default:
			if (ready) goto jmp;
			continue;
		}
		if (ready) {
		jmp:
			buf[i++] = result;
			result = 0; ready = 0;
			continue;
		}
		ready = 1;
	}
	realloc(buf, data_len = i);
	return true;
}

bool send_alarm_time(Value chat_id) {
	if (readFile(ALARM_PATH, timestamp_arr, timestamps_count)) {
		struct tm timeinfo; uint32_t count, str_size = 18 * timestamps_count, heap = ESP.getFreeHeap() - 5000;
		DEBUG("timestamps_count: "); DEBUGLN(timestamps_count);
		if (heap < str_size) { count = heap / 18; str_size = count * 18; }
		else { count = timestamps_count; } DEBUGLN(timestamps_count);
		char* str = new char[str_size];
		for (byte i = 0;;) {
			localtime_r(&timestamp_arr[i], &timeinfo); DEBUGLN(timestamp_arr[i]);
			strftime(&str[18 * i], 18, "%H:%M:%S %d.%m.%y", &timeinfo);
			if (++i < count) str[18 * i - 1] = '\n'; else break;
		}  DEBUGLN(str);
		if (bot.sendMessage(Message(str, chat_id))) { if (interrupt_flag != CHECK_MSG) timer_alarm(TIMER_CHECK, CHECK_MSG); }
		else timer_alarm(TIMER_RESEND, RESEND_MSG); DEBUG("interrupt_flag "); DEBUGLN(interrupt_flag);
		delete[] timestamp_arr; timestamp_arr = nullptr; delete[] str; timestamps_count = 0; return true;
	}
	return false;
}

void updateHandler(fb::Update& u) {
	if (u.isMessage() && u.message().from().id() == USER_ID) {
		timerStop(timer1);
		if (u.message().hasDocument() && u.message().document().name().endsWith(".bin"))
			handleDocument(u);
		else handleMessage(u);
		timerStart(timer1);
	}
}

void otaBegin(fb::Update& u, bool fw = true) {
	Text chat = u.message().chat().id();
	bot.sendMessage(Message("OTA begin", chat));
	Fetcher fetch = bot.downloadFile(u.message().document().id());
	dWrite(PIN_LED, LED_ON);
	if (fetch) {
		bool no_err;
		if (fw) no_err = fetch.ota();
		else no_err = fetch.updateFS();
		if (no_err) {
			bot.sendMessage(Message("Done", chat));
			status = RESTART;
			goto exit;
		}
	}bot.sendMessage(Message("Error", chat));
exit:
	dWrite(PIN_LED, LED_OFF);
}

void handleDocument(fb::Update& u) {
	switch (u.message()[tg_apih::caption].hash()) {
	case SH("/update_fw"): otaBegin(u); break;
	case SH("/update_fs"): otaBegin(u, false); break;
	default: bot.sendMessage(Message("Unknown", u.message().chat().id()));
	}
}

void handleMessage(fb::Update& u) {
	Text chat = u.message().chat().id(); DEBUGLN(u.message().text());
	switch (u.message().text().hash()) {
	case SH("/connect"):
		bot.sendMessage(Message("ESP will stay connected", chat));
		timer_alarm(TIMER_CHECK, CHECK_MSG); break;
	case SH("/disconnect"):
		bot.sendMessage(Message("Disconnecting...", chat));
		status = WIFI_DISCONNECT; break;
	case SH("/get_info"):
		bot.sendMessage(Message(get_info(), chat)); break;
#if defined RELAY
	case SH(RELAY_ON):
		dWrite(PIN_RELAY, HIGH);
		bot.sendMessage(Message("RELAY ON", chat)); break;
	case SH(RELAY_OFF):
		dWrite(PIN_RELAY, LOW);
		bot.sendMessage(Message("RELAY OFF", chat)); break;
#endif
	case SH("/restart"):
		bot.sendMessage(Message("ESP restarting...", chat)); bot.reboot();
		status = RESTART; break;
	case SH("/ble"): bot.sendMessage(Message(ble_advertising(ble_data, ble_data_size) ?
		"BLE data sended" : "BLE data empty", chat)); break;
	case SH("/send_alarm"):
		if (!send_alarm_time(chat)) bot.sendMessage(Message("No file", chat)); break;
	case SH("/clear_alarm"):
		bot.sendMessage(Message(deleteFile(ALARM_PATH) ? "Done" : "No file", chat)); break;
	default: {
		if (u.message().text().startsWith(BLE_SET)) {
			if (strtoB(u.message().text(), sizeof(BLE_SET), ble_data, ble_data_size)) {
				bot.sendMessage(Message(create_hex_string(ble_data, ble_data_size), chat));
			}
			else bot.sendMessage(Message("Wrong format", chat));
		}
		else bot.sendMessage(Message("Unknown", chat)); }
	}
}

void setup() {
#ifdef DEBUG_ENABLE
	Serial.begin();
#endif // DEBUG_ENABLE
	pinMode(PIN_LINE, INPUT); //pinMode(5, INPUT_PULLUP); 
	pinMode(PIN_LED, OUTPUT); //pinMode(PIN_RELAY, OUTPUT);
#ifdef ESP32C3_LUATOS
	dWrite(PIN_LED, LED_OFF); pinMode(PIN_LED_D5, OUTPUT); dWrite(PIN_LED_D5, LED_ON);
#else
	dWrite(PIN_LED, LED_ON);
#endif
	if (!SPIFFS.begin()) DEBUGLN("\nAn error has occurred while mounting SPIFFS");
	WiFi.mode(WIFI_MODE_APSTA);
	wifi_server_init();
	read_credentials();
	wifi_sta_init();
#ifdef DEBUG_ENABLE
	WiFi.printDiag(Serial);
#endif 
	configTime(3 * 3600, 0, "ru.pool.ntp.org", "pool.ntp.org"); time_sync();
	//client.setCACert(TELEGRAM_CERTIFICATE_ROOT);  //api.telegram.org
	bot.setPollMode(Poll::Long, 30000);
	bot.attachUpdate(updateHandler);
	bot.setToken(F(BOT_TOKEN));
	bot.skipUpdates();
	timers_init(TIMER_SABOTAGE, TIMER_CHECK);
	attachInterrupt(PIN_LINE, ISR, FALLING);
	bot.sendMessage(Message(get_info(), CHAT_ID));
	(void)send_alarm_time(CHAT_ID);
	while (last_interrupt == 0 && uS < 2400000) { taskYIELD(); };
	status = LINE_OK;
#ifdef ESP32C3_LUATOS
	dWrite(PIN_LED_D5, LED_OFF);
#else
	dWrite(PIN_LED, LED_OFF);
#endif
	//esp_ota_mark_app_valid_cancel_rollback();
}

void loop() {
	for (;;) {
		if (status == prev_status) continue;
		if (status != CHECK_MSG) { DEBUG("status "); DEBUGLN(status); }
		switch (status) {
		case LINE_OK: tg_send("OK"); continue;
		case ALARM: tg_send("ALARM"); continue;
		case LINE_HIGH: tg_send("LINE_HIGH"); continue;
		case LINE_LOW: tg_send("LINE_LOW"); continue;
		case CHECK_MSG: status = prev_status;
			if (WiFi.isConnected() || wifi_sta_init()) bot.tick(); continue;
		case WIFI_DISCONNECT: status = prev_status;
			time_sync(); timer_alarm(TIMER_RECONNECT, WIFI_DISCONNECT); WiFi.disconnect(); continue;
		case WIFI_INIT: status = prev_status;
			if (wifi_sta_init()) { time_sync(); timer_alarm(TIMER_CHECK, CHECK_MSG); } continue;
		case WIFI_RECON: status = prev_status;
			if (!wifi_sta_init()) continue; bot.tickManual();
			if (interrupt_flag == WIFI_RECON) { time_sync(); WiFi.disconnect(); } continue;
		case RESEND_MSG: status = prev_status;
			if (WiFi.isConnected() || wifi_sta_init()) send_alarm_time(CHAT_ID); continue;
		case RESTART: bot.tickManual(); ESP.restart();
		}
	}
}


