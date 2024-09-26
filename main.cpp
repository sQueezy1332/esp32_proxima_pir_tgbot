#include <MAIN.h>
#include "esp32_pir_tg_bot.h"
void mainTask(void* pvParameters) {
	setup();
	for (;;) {
		while (status == prev_status) {};
		/*if (status != CHECK_MSG) { DEBUG("status "); DEBUGLN(status); }*/
		switch (status) {
		case LINE_OK: tg_send("OK"); continue;
		case ALARM: tg_send("ALARM"); continue;
		case LINE_HIGH: tg_send("LINE_HIGH"); continue;
		case LINE_LOW: tg_send("LINE_LOW"); continue;
		case CHECK_MSG: status = prev_status;
			if (wifi_sta_init()) bot.tick(); continue;
		case WIFI_DISCONNECT: status = prev_status;
			time_sync(); timerAlarm(TIMER_RECONNECT, WIFI_RECON);
			WiFi.disconnect(); continue;
		case WIFI_INIT: status = prev_status;
			if (wifi_sta_init()) { time_sync(); timerAlarm(TIMER_CHECK, CHECK_MSG); } continue;
		case WIFI_RECON: status = prev_status;
			if (wifi_sta_init()) {
				bot.tickManual();
				if (interrupt_flag == WIFI_RECON) { time_sync(); WiFi.disconnect(); }
			} continue;
		case RESEND_MSG: status = prev_status;
			if (wifi_sta_init()) send_alarm_time(CHAT_ID); continue;
		case RESTART: bot.tickManual(); delay(1000); ESP.restart();
		}
	}
}

void setup() {
	pinMode(PIN_LINE, INPUT); //pinMode(5, INPUT_PULLUP); 
	pinMode(PIN_LED, OUTPUT); //pinMode(PIN_RELAY, OUTPUT);
	attachInterrupt(PIN_LINE, &ISR, FALLING);
	//TickType_t wake = xTaskGetTickCount();
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
	configTime(3 * 3600, 0, "ru.pool.ntp.org", "pool.ntp.org");
	time_sync();
	//client.setCACert(TELEGRAM_CERTIFICATE_ROOT);  //api.telegram.org
	bot.setPollMode(Poll::Long, 30000);
	bot.attachUpdate(updateHandler);
	bot.setToken(F(BOT_TOKEN));
	bot.skipUpdates();
	CHECK_(timer_init(TIMER_SABOTAGE, tmrSab, &sabotage_check));
	CHECK_(timer_init(TIMER_CHECK, tmrWifi, &wifi_check));
	bot.sendMessage(Message(get_info(), CHAT_ID));
	(void)send_alarm_time(CHAT_ID);
#ifdef ESP32C3_LUATOS
	dWrite(PIN_LED_D5, LED_OFF);
#else
	dWrite(PIN_LED, LED_OFF);
#endif
	if (img_state(false) == ESP_OTA_IMG_PENDING_VERIFY) bot.sendMessage(Message(((String)"ESP_OTA_IMG_PENDING_VERIFY\n" + __DATE__ + '\t' + __TIME__), CHAT_ID)); 
	while (last_interrupt == 0 && uS < 2400000) {}; status = LINE_OK;
	log_d("SETUP END");
}
extern "C" void app_main() {
	main_init();
	mainTaskHandle = xTaskCreateStaticPinnedToCore(mainTask, "mainTask", MAIN_TASK_STACK_SIZE, NULL, 2, xMainStack, &xMainTaskBuffer, ARDUINO_RUNNING_CORE);
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
		timerAlarm(TIMER_CHECK, CHECK_MSG); break;
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
	case SH("/ble_clear"): free(ble_data); ble_data = nullptr; ble_data_size = 0;
		bot.sendMessage(Message("Done", chat)); break;
	case SH("/send_alarm"):
		if (!send_alarm_time(chat)) bot.sendMessage(Message("No file", chat)); break;
	case SH("/clear_alarm"):
		bot.sendMessage(Message(deleteFile(ALARM_PATH) ? "Done" : "No file", chat)); break;
	case SH("/valid"):  esp_ota_mark_app_valid_cancel_rollback();
		bot.sendMessage(Message(String(img_state()), chat)); break;
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

void updateHandler(fb::Update& u) {
	if (u.isMessage() && u.message().from().id() == USER_ID) {
		timer_stop(tmrWifi);
		if (u.message().hasDocument() && u.message().document().name().endsWith(".bin"))
			handleDocument(u);
		else handleMessage(u);
		timer_start(tmrWifi);
	}
}

static void IRAM_ATTR ISR() {
	//if (lineRead == LOW) {
	static uint32_t delta;
	delta = uS - last_interrupt;
	last_interrupt = uS;
	if (delta < 2400000 && delta > 1000) {
		status = ALARM;
		alarm_delta = delta;
	}
	else if (prev_status == status) { interrupt_delta = delta; status = LINE_OK; }
	timer_restart(tmrSab);
	//} else if (uS - last_interrupt > 2000) status = LINE_LOW;
}

bool IRAM_ATTR sabotage_check(gptimer_handle_t tmr, const gptimer_alarm_event_data_t* edata, void* user_ctx) {
	lineRead ? status = LINE_HIGH : status = LINE_LOW;
	return false;
}

bool IRAM_ATTR wifi_check(gptimer_handle_t tmr, const gptimer_alarm_event_data_t* edata, void* user_ctx) {
	if (prev_status == status) status = interrupt_flag; 
	return false;
}

static void timerAlarm(uint64_t value, stat_t flag, gptimer_handle_t& handle) {
	timer_restart(handle); timer_alarm(value, handle);
	interrupt_flag = flag;
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

void time_sync(byte wait_sec) {
	TickType_t ticker = xTaskGetTickCount();
	time((time_t*)&timestamp_unix);
	while (timestamp_unix < 1000000000) {
		vTaskDelayUntil(&ticker, pdMS_TO_TICKS(1000));
		time((time_t*)&timestamp_unix);
		if (--wait_sec == 0) {
			DEBUGLN("\nTime sync failed!");
			return;
		}
	}
	timestamp_sync = uS; log_d("%u", timestamp_unix);
}


bool readFile(cch* path, String*& Content) {
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

bool writeFile(cch* path, String*& Content) {
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

bool readFile(cch* path, _time_t*& buf, size_t& size) {
	DEBUG("Reading file: "); DEBUGLN(path);
	fs::File file = SPIFFS.open(path, FILE_READ);
	if (!file || file.isDirectory() || !file.available()) {
		DEBUGLN(" failed to open file for reading");
		return false;
	}log_d("file size %u", file.size());
	if (size_t count = file.size() / sizeof(_time_t)) {
		delete[] buf; size = count;
		buf = new _time_t[count];
		file.read((byte*)buf, count * sizeof(_time_t));
		return true;
	}
	return false;
}

bool appendFile(cch* path, _time_t value) {
	DEBUG("Append file: "); DEBUG(path);
	fs::File file = SPIFFS.open(path, FILE_APPEND);
	if (!file) {
		DEBUGLN(" failed to open file for writing");
		return false;
	}
	if (file.write((byte*)&value, sizeof(_time_t))) {
		DEBUGLN(" file written");
		return true;
	}
	else {
		DEBUGLN(" write failed");
		return false;
	}
}

bool deleteFile(cch* path) {
	DEBUG("Deleting file: "); DEBUG(path);
	if (SPIFFS.remove(path)) {
		DEBUGLN(" file deleted");
		return true;
	}
	else DEBUGLN(" delete failed"); return false;
}

String get_info() {
	uint32_t heap = ESP.getFreeHeap(); uint32_t sec = uS / 1000000; String str; str.reserve(160);
	str += "Connected to: "; str += *ssid; str += "\nLocal IP: "; str += WiFi.localIP().toString(); str += "\nRSSI: "; str += WiFi.RSSI();
	str += "\nFree Heap: "; str += heap; str += "\nStack watermark "; str += uxTaskGetStackHighWaterMark2(NULL);
	str += "\nUptime: "; str += sec / 3600 / 24;  str += "d "; str += sec / 3600 % 24; str += "h "; str += sec / 60 % 60;
	str += "m "; str += sec % 60; str += 's'; str += "\ninterrupt_delta =  "; str += interrupt_delta; log_d("%u", str.length());
	return str;
}

void onWiFiConnected(arduino_event_id_t event) {
	if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
		status = RESEND_MSG;
	}
}

bool wifi_sta_init(byte wait_sec) {
	if (!WiFi.isConnected()) {
		WiFi.begin(*ssid, *pass); log_d("Wait connection for %u sec...", wait_sec);
		//WiFi.waitStatusBits(0x00FFFFFF, 10000);
		uint64_t timestamp = uS + (wait_sec * 1000000ul);
		for (wl_status_t stat = WiFi.status(); stat != WL_CONNECTED; stat = WiFi.status()) {
#ifdef DEBUG_ENABLE
			delay(1000); DEBUG(stat); DEBUG(' ');
#else
			delay(100);
#endif 
			if (uS > timestamp) { log_d("Not connected"); return false; }
		} DEBUGLN();
	}
	return true;
}

void wifi_server_init() {
#if	WIFI_CHANNEL > 11
	ERR_CHECK(esp_wifi_set_country_code("CN", false));
#endif
	WiFi.softAP(AP_SSID, AP_PASS, WIFI_CHANNEL, SSID_HIDDEN);
	//for (uint64_t timer = uS + 1000000; !(WiFi.getStatusBits() & AP_STARTED_BIT);) { if (uS > timer) ESP.restart(); }
	WiFi.setTxPower(WIFI_POWER_20dBm); DEBUGLN(WiFi.getTxPower()); //WIFI_POWER_20dBm = 80,// 20dBm
	WiFi.softAPbandwidth(WIFI_BW_HT20);
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
		status = WIFI_DISCONNECT;
		});
	server.on("/restart", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Esp restarting...");
		status = RESTART;
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

void tg_send(cch* text, cch* chat) {
	static stat_t _status; static Message msg("", chat);
	_status = status; dWrite(PIN_LED, LED_ON); 
	if (!wifi_sta_init()) {
		if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
		goto save;
	}
	else {
		msg.text = text; DEBUGLN(msg.text);
		if (_status == ALARM) { msg.text += '\t'; msg.text += alarm_delta / 1000; }
		if (!bot.sendMessage(msg)) {
		save:
			if (_status != LINE_OK) {
				appendFile(ALARM_PATH, timestamp_unix + ((uS - timestamp_sync) / 1000000));
				timerAlarm(TIMER_RESEND, RESEND_MSG);
			}
		}
	}
	prev_status = _status; dWrite(PIN_LED, LED_OFF);
}

bool ble_advertising(const byte* ble_data, const byte ble_data_length, uint32_t time_ms) {
	if (ble_data == nullptr || ble_data_length == 0) return false; DEBUGLN(ESP.getFreeHeap());
	return true;
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

bool strtoB(const String& str, byte sub, byte*& buf, byte& data_len, const byte hexSizeMin) {
	unsigned str_len = str.length() - sub, hex_len = (str_len + 1) / 2; //DEBUGLN(hex_len);
	if (hex_len < hexSizeMin || hex_len > 255) return false;
	cch* ptr = str.c_str() + sub;
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

bool send_alarm_time(Value const& chat_id) {
	if (readFile(ALARM_PATH, timestamp_arr, timestamps_count)) {
		struct tm timeinfo; uint32_t count, str_size = 18 * timestamps_count, heap = ESP.getFreeHeap() - 5000;
		if (heap < str_size) { count = heap / 18; str_size = count * 18; log_d("count: %u", count); }
		else { count = timestamps_count; } log_d("timestamps_count: %u", timestamps_count); 
		Message* msg = new Message("", chat_id); msg->text.reserve(str_size);
		for (byte i = 0;;) {
			localtime_r((time_t*)&timestamp_arr[i], &timeinfo); DEBUGLN(timestamp_arr[i]);
			strftime(&msg->text[18 * i], 18, "%H:%M:%S %d.%m.%y", &timeinfo);
			if (++i < count) msg->text[18 * i - 1] = '\n'; else break;
		}  DEBUGLN(msg->text);
		if (!wifi_sta_init()) {
			if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
			goto resend;
		}
		else if (bot.sendMessage(*msg)) {
			timerAlarm(TIMER_CHECK, CHECK_MSG);
			if (event_id) { WiFi.removeEvent(event_id); event_id = 0; }
		}
		else {
		resend:
			if (interrupt_flag != RESEND_MSG) timerAlarm(TIMER_RESEND, RESEND_MSG);
		}log_d("interrupt_flag %u", interrupt_flag);
		delete[] timestamp_arr; timestamp_arr = nullptr; delete msg; timestamps_count = 0; return true;
	}
	return false;
}

void otaBegin(fb::Update& u, bool fw) {
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
