#include "esp32_pir_tg_bot.h"

extern "C" void app_main() {
	main_init();
	QueueStatHandle = xQueueCreateStatic(QUEUE_SIZE, QUEUE_ITEM_SIZE, &QueueStatStorage[0], &pxStaticQueue);
	xTaskCreate(setup, "setup", 8192, NULL, 5, NULL);
}

void mainTask(void*) {
	for (TickType_t tick = 0;;) {
		switch (Flag) {
		case RESEND_MSG:
			if (xTaskGetTickCount() - tick > pdMS_TO_TICKS(30 * 60 * 1000)) {
				Flag = send_alarm_time(CHAT_ID); tick = xTaskGetTickCount();
			}
		case CHECK_MSG:
			if (wifi_sta_init()) { bot.tick(); }
			delay(1000); continue;
		case WIFI_DISCONNECT:
			time_sync(); WiFi.disconnect(); Flag = WIFI_RECON;
			break;
		case WIFI_INIT: 
			Flag = CHECK_MSG;
		case WIFI_RECON:
			if (wifi_sta_init()) {
				bot.tickManual();
				time_sync();
				if (Flag != WIFI_RECON) continue;
			} break;
		case RESTART: bot.tickManual(); yield(); ESP.restart();
		default:Flag = CHECK_MSG; continue;
		}
		vTaskDelayUntil(&tick, pdMS_TO_TICKS(60 * 60 * 1000));
	}
}

void sendTask(void*) {
	for (tgMessage_t tmp;;) {
		while (xQueueReceive(QueueStatHandle, &tmp, portMAX_DELAY) == pdPASS) {
			tg_send(tmp);
		}
	}
}
/*		INTERRUPTS		*/
static void IRAM_ATTR ISR() {
	uint64_t time = uS; uint32_t delta = time - last_interrupt;
	last_interrupt = time; interrupt_delta = delta; tgMessage_t tmp;
	timer_restart(tmr_sab);
	if (alarm_state && (delta < 2400000) && (delta > 1000)) {
		tmp = { .delta = delta, .status = ALARM, };
		//prev_alarm = ALARM;
	} /*else if (prev_alarm != ok) {
		tmp = { .delta = delta, .status = ok, }; prev_alarm = ok;
	} */else return;
	xQueueSendFromISR(QueueStatHandle, &tmp, nullptr);
}

bool IRAM_ATTR sabotage_check(gptimer_handle_t tmr, const gptimer_alarm_event_data_t* edata, void* user_ctx) {
	tgMessage_t tmp {
		.delta = uS - last_interrupt,
		.status = lineRead ? LINE_HIGH : LINE_LOW
	};
	//prev_alarm = tmp.status;
	xQueueSendFromISR(QueueStatHandle, &tmp, nullptr);	
	return false;
}
/*		INIT	*/
void setup(void*) {
	pinMode(PIN_LINE, INPUT_PULLUP); //pinMode(PIN_PULLUP, OUTPUT); dWrite(PIN_PULLUP, 1);
	pinMode(PIN_LED, OUTPUT); //pinMode(PIN_RELAY, OUTPUT);
#ifdef ESP32C3_LUATOS
	pinMode(PIN_LED_D5, OUTPUT); dWrite(PIN_LED_D5, LED_OFF);
#endif
	dWrite(PIN_LED, LED_ON);
	CHECK_(timer_init(TIMER_SABOTAGE, tmr_sab, &sabotage_check, 1, 0));
	attachInterrupt(PIN_LINE, &ISR, FALLING);// gpio_install_isr_service((int)ARDUINO_ISR_FLAG);
	if (!SPIFFS.begin()) DEBUGLN("\nAn error has occurred while mounting SPIFFS");
	WiFi.mode(WIFI_MODE_APSTA);
	wifi_server_init();
	read_credentials();
	wifi_sta_init();
#ifdef DEBUG_ENABLE
	WiFi.printDiag(Serial); log_d("sizeof(QueueStatStorage) %u ", sizeof(QueueStatStorage));
#endif 
	configTime(3 * 3600, 0, "ru.pool.ntp.org", "pool.ntp.org");
	time_sync();
	//client.setCACert(TELEGRAM_CERTIFICATE_ROOT);  //api.telegram.org
	bot.setToken(F(BOT_TOKEN));
	bot.attachUpdate(updateHandler);
	//bot.setPollMode(Poll::Long, 20000);
	bot.skipUpdates();
	bot.sendMessage(Message(get_info(), CHAT_ID));
	Flag = send_alarm_time(CHAT_ID);
	if (img_state(false) == ESP_OTA_IMG_PENDING_VERIFY) bot.sendMessage(Message(((String)"ESP_OTA_IMG_PENDING_VERIFY\n" + __DATE__ + '\t' + __TIME__), CHAT_ID));
	mainTaskHandle = xTaskCreateStatic(mainTask, "main", sizeof(xMainStack), NULL, 4, xMainStack, &xMainTaskBuffer);
	sendTaskHandle = xTaskCreateStatic(sendTask, "send", sizeof(xSendStack), NULL, 5, xSendStack, &xSendTaskBuffer);
	alarm_on(); dWrite(PIN_LED, LED_OFF); log_i("SETUP END");
	vTaskDelete(NULL);
}

void time_sync(byte wait_sec) {
	DEBUG("Time sync -");
	for (TickType_t ticker = 0;; --wait_sec) {
		time((time_t*)&timestamp_unix);
		if (timestamp_unix > 1000000000) break;
		if (wait_sec == 0) {
			DEBUGLN(" failed!");
			return;
		}
		vTaskDelayUntil(&ticker, pdMS_TO_TICKS(1000));
	}
	timestamp_sync = uS; DEBUGLN(timestamp_unix);
}
/*		FILE SYSTEM	*/
stat_t send_alarm_time(Value const& chat_id) {
	DEBUG("Reading file: "); DEBUGLN(ALARM_PATH);
	fs::File file = SPIFFS.open(ALARM_PATH, FILE_READ);
	if (!file || file.isDirectory() || !file.available()) {
		DEBUGLN(" failed to open file for reading");
		return ok;
	}
	struct tm timeinfo; _time_t timestamp = 0; Message msg("", chat_id); char* tmp = &msg.text[0];
	const uint32_t file_size = file.size(), count = file_size / sizeof(_time_t)
	, str_size = 11/*18*/ * count, heap = ESP.getFreeHeap(); log_d("file_size %u, count %u, str_size %u, HEAP %u", file_size, count, str_size, heap);
	if (heap - 5000 < str_size || !msg.text.reserve(str_size + 1)) { DEBUGLN("Not enough heap"); return ok; }
	/*for (size_t offset = 0; offset < str_size, file.available();) {
		file.read((byte*)&timestamp, sizeof(_time_t));
		localtime_r((time_t*)&timestamp, &timeinfo); DEBUGLN(timestamp);
		strftime(&tmp[offset], 18, "%H:%M:%S %d.%m.%y", &timeinfo);
		offset += 18;
		msg.text[offset - 1] = '\n';
	}  msg.text[str_size - 1] = '\0';*/ /////TODO
	for (size_t i = 0; i < count, file.available(); i++) {
		file.read((byte*)&timestamp, sizeof(_time_t)); DEBUGLN(timestamp);
		msg.text += timestamp;
		msg.text += '\n';
	}  msg.text[str_size - 1] = '\0';
	log_d("%s", msg.text.c_str()); 
	if (!wifi_sta_init()) {
		if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
		return RESEND_MSG;
	} else if (!bot.sendMessage(msg)) return RESEND_MSG;
	if (event_id) { WiFi.removeEvent(event_id); event_id = 0; }
	return CHECK_MSG;
}

bool readFile(cch* path, String& Content) {
	DEBUG("Reading file: "); DEBUGLN(path);
	fs::File file = SPIFFS.open(path, FILE_READ);
	if (!file || file.isDirectory() || !file.available()) {
		DEBUGLN(" failed to open file for reading");
		return false;
	}
	Content = file.readStringUntil('\0');
	return true;
}

bool writeFile(cch* path, const String& Content) {
	DEBUG("Writing file: "); DEBUG(path);
	fs::File file = SPIFFS.open(path, FILE_WRITE);
	if (!file) {
		DEBUGLN(" failed to open file for writing");
		return false;
	}
	if (file.print(Content)) {
		DEBUGLN(" file written");
		return true;
	} else {
		DEBUGLN(" write failed");
		return false;
	}
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
	} else {
		DEBUGLN(" write failed");
		return false;
	}
}

bool deleteFile(cch* path) {
	DEBUG("Deleting file: "); DEBUG(path);
	if (SPIFFS.remove(path)) {
		DEBUGLN(" file deleted");
		return true;
	} else DEBUGLN(" delete failed"); return false;
}

String get_info() {
	uint32_t heap = ESP.getFreeHeap(); uint32_t sec = uS / 1000000; String str; str.reserve(180);
	str += "Connected to: "; str += ssid; str += "\nLocal IP: "; str += WiFi.localIP().toString(); str += "\nRSSI: "; str += WiFi.RSSI();
	str += "\nFree Heap: "; str += heap; str += "\nStack watermark:"; str += "\nmainTask "; str += uxTaskGetStackHighWaterMark2(NULL); 
	str += "\nsendTask "; str += uxTaskGetStackHighWaterMark2(sendTaskHandle);
	str += "\nUptime: "; str += sec / 3600 / 24;  str += "d "; str += sec / 3600 % 24; str += "h "; str += sec / 60 % 60;
	str += "m "; str += sec % 60; str += 's'; str += "\ninterrupt_delta =  "; str += interrupt_delta; log_d("%u", str.length());
	return str;
}
/*		WIFI	*/
void onWiFiConnected(arduino_event_id_t event) {
	if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
		resumeTask(RESEND_MSG);
		yield();
	}
}

bool wifi_sta_init(byte wait_sec) {
	if (!WiFi.isConnected()) {
		WiFi.begin(ssid, pass); log_i("Wait connection %u sec...", wait_sec);
		//WiFi.waitStatusBits(0x00FFFFFF, 10000);
		auto timestamp = xTaskGetTickCount() + pdMS_TO_TICKS(wait_sec * 1000);
		for (wl_status_t stat = WiFi.status(); stat != WL_CONNECTED; stat = WiFi.status()) {
#ifdef DEBUG_ENABLE
			delay(1000); DEBUG(stat); DEBUG(' ');
#else
			delay(100);
#endif 
			if (xTaskGetTickCount() > timestamp) { log_i("Not connected"); return false; }
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
		String str = "Connecting to:\n"; "SSID = ["; str += ssid; str += "]\n"; str += "PASS = ["; str += pass; str += "]\n";
		request->send(200, "text/plain", str);
		resumeTask(WIFI_INIT);
		});
	server.on("/disconnect", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Disconnecting...");
		resumeTask(WIFI_DISCONNECT);
		});
	server.on("/restart", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Esp restarting...");
		resumeTask(RESTART);
		});
	server.on("/alarm_on", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Alarm on");
		alarm_on();
		});
	server.on("/alarm_off", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Alarm off");
		alarm_off();
		});
	server.on(CHANGE_AUTH, HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(SPIFFS, "/config.html", "text/html");
		});
	server.on(CHANGE_AUTH, HTTP_POST, onConfigRequest);
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
		ssid = pSSID->value(); //pSSID->value().length()
		if (!writeFile(SSID_PATH, ssid)) {
			log += "ERROR WRITE "; log += SSID_PATH; log += '\n';
		}
	}
	if (!wrongPASS) {
		pass = pPASS->value();
		if (!writeFile(PASS_PATH, pass)) {
			log += "ERROR WRITE ";  log += PASS_PATH; log += '\n';
		}
	}/////DEBUGf("POST[%s]: %s\n", pSSID->name().c_str(), pSSID->value().c_str(), pPASS->name().c_str(), pPASS->value().c_str());
	log += "SSID = ["; log += ssid; log += "]\n"; log += "PASS = ["; log += pass; log += "]\n";
	log += "Done. Connecting with new credentials"; DEBUGLN(log);
	request->send(200, "text/plain", log);
	resumeTask(WIFI_INIT);
}
/*		TELEGRAM	*/
void tg_send(tgMessage_t& tmp) {
	dWrite(PIN_LED, LED_ON);
	Message msg("", CHAT_ID);
	if (!wifi_sta_init()) {
		if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
		goto save;
	} else {
		switch (tmp.status) {
		//case ok: msg.text = "OK"; break;
		case ALARM: msg.text = "ALARM"; break;
		case LINE_HIGH: msg.text = "LINE_HIGH"; break;
		case LINE_LOW:  msg.text = "LINE_LOW"; break; 
		default: msg.text = tmp.status;
		} DEBUGLN(msg.text);
		//if (tmp.status != ok) 
		{ msg.text += '\t'; msg.text += (tmp.delta / 1000); }
		DEBUGLN(xTaskGetTickCount()); //while (bot.isPolling()){ delay(100); }
		if (!bot.sendMessage(msg)) {
save:
		//	if (tmp.status != ok)
			{	appendFile(ALARM_PATH, timestamp_unix + ((uS - timestamp_sync) / 1000000));
				Flag = RESEND_MSG;
			}
		}
	}DEBUGLN(xTaskGetTickCount());
	dWrite(PIN_LED, LED_OFF);
}

void handleMessage(fb::Update& u) {
	auto chat = u.message().chat().id(); DEBUGLN(u.message().text());
	switch (u.message().text().hash()) {
	case SH("/connect"):
		bot.sendMessage(Message("ESP will stay connected", chat));
		Flag = CHECK_MSG; break;
	case SH("/disconnect"):
		bot.sendMessage(Message("Disconnecting...", chat));
		Flag = (WIFI_DISCONNECT); break;
	case SH("/alarm_on"):
		bot.sendMessage(Message("Alarm on", chat));
		alarm_on(); break;
	case SH("/alarm_off"):
		bot.sendMessage(Message("Alarm off", chat));
		alarm_off(); break;
	case SH("/get_info"):
		bot.sendMessage(Message(get_info(), chat)); break;
	case SH("/restart"):
		bot.sendMessage(Message("ESP restarting...", chat)); 
		bot.reboot(); Flag = RESTART; break;

	case SH("/send_alarm"):
		if (!send_alarm_time(chat)) bot.sendMessage(Message("No file", chat)); break;
	case SH("/clear_alarm"):
		bot.sendMessage(Message(deleteFile(ALARM_PATH) ? "Done" : "No file", chat)); break;
	case SH("/valid"):  esp_ota_mark_app_valid_cancel_rollback();
		bot.sendMessage(Message(String(img_state()), chat)); break;
#if defined RELAY
	case SH(RELAY_ON):
		dWrite(PIN_RELAY, HIGH);
		bot.sendMessage(Message("RELAY ON", chat)); break;
	case SH(RELAY_OFF):
		dWrite(PIN_RELAY, LOW);
		bot.sendMessage(Message("RELAY OFF", chat)); break;
#endif
#ifndef NO_BLE
	case SH("/ble"): bot.sendMessage(Message(ble_advertising(ble_data, ble_data_size) ?
		"BLE data sended" : "BLE data empty", chat)); break;
	case SH("/ble_clear"): free(ble_data); ble_data = nullptr; ble_data_size = 0;
		bot.sendMessage(Message("Done", chat)); break;

	default: {
		if (u.message().text().startsWith(BLE_SET)) {
			if (strtoB(u.message().text(), sizeof(BLE_SET), ble_data, ble_data_size)) {
				bot.sendMessage(Message(create_hex_string(ble_data, ble_data_size), chat));
			} else bot.sendMessage(Message("Wrong format", chat));
		} else bot.sendMessage(Message("Unknown", chat)); }
#endif
	}

}

void handleDocument(fb::Update& u) {
	switch (u.message()[tg_apih::caption].hash()) {
	case SH("/update_fw"): otaBegin(u, true); break;
	case SH("/update_fs"): otaBegin(u, false); break;
	default: bot.sendMessage(Message("Unknown", u.message().chat().id()));
	}
}

void otaBegin(fb::Update& u, bool fw) {
	dWrite(PIN_LED, LED_ON);
	auto chat = u.message().chat().id();
	if (fw) bot.updateFlash(u.message().document(), chat);
	else bot.updateFS(u.message().document(), chat);
	dWrite(PIN_LED, LED_OFF);
}

void updateHandler(fb::Update& u) {
	if (u.isMessage() && u.message().from().id() == USER_ID) {
		switch (Flag) {
		case RESEND_MSG: Flag = send_alarm_time(CHAT_ID);
		case CHECK_MSG: break;
		default:Flag = CHECK_MSG;
		}
		if (u.message().hasDocument() && u.message().document().name().endsWith(".bin"))
			handleDocument(u);
		else handleMessage(u);
	}
}
/*		BLUETOOTH		*/
#ifndef NO_BLE
bool ble_advertising(const byte* ble_data, const byte ble_data_length, uint32_t time_ms) {
	if (ble_data == nullptr || ble_data_length == 0) return false; DEBUGLN(ESP.getFreeHeap());
	esp_ble_gap_ext_adv_params_t ext_adv_params_coded = {
	.type = ESP_BLE_GAP_SET_EXT_ADV_PROP_NONCONN_NONSCANNABLE_UNDIRECTED,
	.interval_min = 0x30,
	.interval_max = 0x30,
	.channel_map = ADV_CHNL_ALL,
	.own_addr_type = BLE_ADDR_TYPE_PUBLIC,
	.filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
	.tx_power = 126,
	.primary_phy = ESP_BLE_GAP_PHY_CODED,
	.max_skip = 0,
	.secondary_phy = ESP_BLE_GAP_PHY_CODED,
	.sid = 1,
	.scan_req_notif = false,
	};
	if (!BLEDevice::getInitialized()) BLEDevice::init("");
	BLEMultiAdvertising advert;
	esp_ble_gap_set_preferred_default_phy(ESP_BLE_GAP_PHY_OPTIONS_PREF_S8_CODING, ESP_BLE_GAP_PHY_OPTIONS_PREF_S8_CODING);
	advert.setAdvertisingParams(0, &ext_adv_params_coded);
	advert.setDuration(0);
	advert.setAdvertisingData(0, ble_data_length, ble_data);
	advert.start(); delay(time_ms); advert.stop(0, (uint8_t*)0); advert.clear();
	BLEDevice::deinit(); DEBUGLN(ESP.getFreeHeap());
	return true;
}
#endif
/*		MISC		*/
void read_credentials() {
	if (!readFile(SSID_PATH, ssid) || ssid.length() == 0
		|| !readFile(PASS_PATH, pass) || pass.length() < 8) {
		DEBUGLN("\nERROR READ WIFI LOGIN"); //delay(2000);
		//if (!digitalRead(PIN_BUTTON)) while (status != WIFI_INIT) { taskYIELD(); }else ESP.restart();
		ssid = DEFAULT_SSID;
		pass = DEFAULT_PASS;
	} DEBUGLN(ssid); DEBUGLN(pass);
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
