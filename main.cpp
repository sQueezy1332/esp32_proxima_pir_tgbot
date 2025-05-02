#include "esp32_pir_tg_bot.h"

extern "C" void app_main() {
	main_init();
	setup((void*)0);
	//xTaskCreate(setup, "setup", 8192, NULL, 6, NULL);
	log_d("%u", uxTaskGetStackHighWaterMark2(NULL));
}

void mainTask(void*) {
	for (TickType_t tick = 0;;) {
		switch (Flag) {
		case RESEND_MSG:
			if (xTaskGetTickCount() - tick > pdMS_TO_TICKS(30 * 60 * 1000)) {
				send_alarm_time(CHAT_ID); tick = xTaskGetTickCount(); log_i("%u", Flag);
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
			tg_send(tmp); delay(1000);
		}
	}
}
/*		INTERRUPTS		*/
static void IRAM_ATTR ISR() {
	uint64_t time = uS; uint32_t delta = time - last_interrupt;
	//static uint64_t last_alarm = 0; static byte alarm_count = 0;
	last_interrupt = time; interrupt_delta = delta; tgMessage_t tmp; //sizeof(tgMessage_t)
	timer_restart(timer_sab);
	if (alarm_state && (delta < 2400000) && (delta > 1000)) {
		//if (time - last_alarm < 15* 60* 1000000) {
			//if(++alarm_count )
			tmp = { .status = ALARM,.delta = (uint16_t)(delta / 1000), };
			//alarm_count = 0;
		//}
		//else { last_alarm = time; return; }
		//prev_alarm = ALARM;
	} /*else if (prev_alarm != ok) {
		tmp = { .status = ok, .delta = delta }; prev_alarm = ok;
	} */else return; 
	xQueueSendFromISR(QueueStatHandle, &tmp, nullptr);
}

bool IRAM_ATTR sabotage_check(gptimer_handle_t tmr, const gptimer_alarm_event_data_t* edata, void* user_ctx) {
	tgMessage_t tmp{
		.status = lineRead ? LINE_HIGH : LINE_LOW,
		.delta = (uint16_t)((uS - last_interrupt) / 1000),
	}; //prev_alarm = tmp.status;
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
	CHECK_(timer_init(TIMER_SABOTAGE, timer_sab, &sabotage_check, 0, 0));
	attachInterrupt(PIN_LINE, &ISR, FALLING);// gpio_install_isr_service((int)ARDUINO_ISR_FLAG);
	if (!SPIFFS.begin()) DEBUGLN("\nAn error has occurred while mounting SPIFFS");
	WiFi.mode(WIFI_MODE_APSTA);
	wifi_server_init();
	read_credentials();
	wifi_sta_init();
#ifdef DEBUG_ENABLE
	WiFi.printDiag(Serial); //log_d("sizeof(QueueStatStorage) %u ", sizeof(QueueStatStorage));
#endif 
	configTime(3 * 3600, 0, "ru.pool.ntp.org", "pool.ntp.org");
	time_sync();
	//client.setCACert(TELEGRAM_CERTIFICATE_ROOT);  //api.telegram.org
	bot.setToken(F(BOT_TOKEN)); bot.attachUpdate(updateHandler); //bot.setPollMode(Poll::Long, 20000);
	bot.skipUpdates();
	Message msg("", CHAT_ID); msg.text = std::move(get_info(true)); 
	if (img_state(false) == ESP_OTA_IMG_PENDING_VERIFY) { msg.text += "ESP_OTA_IMG_PENDING_VERIFY"; }
	bot.sendMessage(msg);
	send_alarm_time(CHAT_ID, false);
	QueueStatHandle = xQueueCreateStatic(QUEUE_SIZE, QUEUE_ITEM_SIZE, &QueueStatStorage[0], &pxStaticQueue);
	loopTaskHandle = xTaskCreateStatic(mainTask, "main", sizeof(xMainStack), NULL, 4, xMainStack, &xMainTaskBuffer);
	sendTaskHandle = xTaskCreateStatic(sendTask, "send", sizeof(xSendStack), NULL, 5, xSendStack, &xSendTaskBuffer);
	alarm_on(); timer_start(timer_sab); dWrite(PIN_LED, LED_OFF);
	//vTaskDelete(NULL);
}

void time_sync(byte wait_sec) {
	DEBUG("Time sync ");
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
void send_alarm_time(Value const& chat_id, bool no_file) {
	DEBUG("Reading file: "); DEBUGLN(ALARM_PATH); Message msg; msg.chatID = chat_id;
	fs::File file = SPIFFS.open(ALARM_PATH, FILE_READ);
	if (!file || file.isDirectory() || !file.available()) {
		DEBUGLN(" failed to open file for reading");
		if (no_file) { msg.text = "No file"; bot.sendMessage(msg); } return;
	}
	const uint32_t file_size = file.size(), count = file_size / sizeof(_time_t), str_len = (18 * count) - 1, heap = ESP.getFreeHeap();
	log_i("file_size %u, count %u, str_len %u, HEAP %u", file_size, count, str_len, heap);
	{String str("", str_len);
	if (heap - 5000 < str_len || !str.length()) {
		log_i("Not enough heap"); msg.text = "Not enough heap"; bot.sendMessage(msg); return;
	} 
	char* с = &str[0]; time_t timestamp = 0; struct tm timeinfo;
	for (size_t offset = 0; offset < str_len, file.available();) {
		file.read((byte*)&timestamp, sizeof(_time_t));
		localtime_r(&timestamp, &timeinfo); DEBUGLN(timestamp); //"%H:%M:%S %d.%m.%y"
		strftime(&с[offset], 18, "%H:%M:%S %d.%m.%y", &timeinfo);
		offset += 18;
		с[offset - 1] = '\n';
	}	с[str_len] = '\0';
	msg.text = std::move(str); DEBUGLN(msg.text); }
	if (!wifi_sta_init()) {
		if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
		Flag = RESEND_MSG; return;
	}
	if (!bot.sendMessage(msg)) { Flag = RESEND_MSG; return; }
	if (event_id) { WiFi.removeEvent(event_id); event_id = 0; }
	Flag = CHECK_MSG;
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
/*		WIFI	*/
void onWiFiConnected(arduino_event_id_t event) {
	if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
		WiFi.removeEvent(event_id); event_id = 0;
		resumeTask();
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
	CHECK_(esp_wifi_set_country_code("CN", false));
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
	log_d("server.begin()");
	server.begin(); // Start server
}

void onConfigRequest(AsyncWebServerRequest* request) {
	const AsyncWebParameter* pSSID = request->getParam(0), * pPASS = request->getParam(1);
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
	static Message msg("", CHAT_ID);
	if (!wifi_sta_init()) {
		if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
		goto save;
	} else {
		switch (tmp.status) {
		//case ok: msg.text = "OK"; break;
		//case ALARM: msg.text = "ALARM"; break;
		case LINE_HIGH: msg.text = "LINE_HIGH"; break;
		case LINE_LOW:  msg.text = "LINE_LOW"; break;
		default: msg.text = "ALARM"; //msg.text = (uint8_t)tmp.status; log_d("default");
		} DEBUGLN(msg.text);
		//if (tmp.status != ok) 
		{ msg.text += '\t'; msg.text += (tmp.delta); }
		DEBUGLN(xTaskGetTickCount()); //while (bot.isPolling()) { delay(100); }
		if (!bot.sendMessage(msg)) {
save:
		//	if (tmp.status != ok)
			{	appendFile(ALARM_PATH, timestamp_unix + ((uS - timestamp_sync) / 1000000));
			Flag = RESEND_MSG;
			}
		}
		if (Flag != RESEND_MSG) Flag = CHECK_MSG;
	}DEBUGLN(xTaskGetTickCount());
	dWrite(PIN_LED, LED_OFF);
}

void handleMessage(fb::Update& u) {
	Message msg; msg.chatID = u.message().chat().id(); DEBUGLN(u.message().text());
	switch (u.message().text().hash()) {
	case SH("/connect"):
		msg.text = "ESP will stay connected";
		bot.sendMessage(msg); Flag = CHECK_MSG; break;
	case SH("/disconnect"):
		msg.text = "Disconnecting..."; bot.sendMessage(msg);
		Flag = WIFI_DISCONNECT; break;
	case SH("/alarm_on"):
		msg.text = "Alarm on"; bot.sendMessage(msg);
		alarm_on(); break;
	case SH("/alarm_off"):
		msg.text = "Alarm off"; bot.sendMessage(msg);
		alarm_off(); break;
	case SH("/get_info"):
		msg.text = std::move(get_info());
		bot.sendMessage(msg); break;
	case SH("/restart"):
		msg.text = "ESP restarting..."; bot.sendMessage(msg);
		bot.reboot(); Flag = RESTART; break;
	case SH("/send_alarm"):
		send_alarm_time(msg.chatID); break;
	case SH("/clear_alarm"):
		msg.text = deleteFile(ALARM_PATH) ? "Done" : "No file";
		bot.sendMessage(msg); break;
	case SH("/valid"):
		esp_ota_mark_app_valid_cancel_rollback();
		msg.text += (int)img_state(); bot.sendMessage(msg);  break;
#if defined RELAY
	case SH(RELAY_ON):
		dWrite(PIN_RELAY, HIGH);
		bot.sendMessage(Message("RELAY ON", chat)); break;
	case SH(RELAY_OFF):
		dWrite(PIN_RELAY, LOW);
		bot.sendMessage(Message("RELAY OFF", chat)); break;
#endif
#ifndef NO_BLE
	case SH("/ble"): bot.sendMessage(Message(
		msg.text = ble_advertising(ble_data, ble_data_size) ? "BLE data sended" : "BLE data empty";
		bot.sendMessage(msg); break;
	case SH("/ble_clear"): free(ble_data); ble_data = nullptr; ble_data_size = 0;
		msg.text = "Done"; bot.sendMessage(msg); break;
	default: {
		if (u.message().text().startsWith(BLE_SET)) {
			if (strtoB(u.message().text(), sizeof(BLE_SET), ble_data, ble_data_size)) {
				msg.text = std::move(create_hex_string(ble_data, ble_data_size));
			} else msg.text = "Wrong format";
			bot.sendMessage(msg);
		} else { msg.text = "Unknown"; bot.sendMessage(msg); }
	}
#endif
	}
}

void handleDocument(fb::Update& u) {
	switch (u.message()[tg_apih::caption].hash()) {
	case SH("/firmware"): otaBegin(u, true); break;
	case SH("/filesystem"): otaBegin(u, false); break;
	default: bot.sendMessage(Message("Unknown", u.message().chat().id()));
	}
}

void otaBegin(fb::Update& u, bool fw) {
	dWrite(PIN_LED, LED_ON);
	vTaskSuspend(sendTaskHandle); timer_stop(timer_sab);
	Message msg("OTA begin", u.message().chat().id()); bool ret;
	bot.sendMessage(msg);
	Fetcher fetch = bot.downloadFile(u.message().document().id());
	if (!fetch) { msg.text = "Download error"; bot.sendMessage(msg); }
	if (fw) ret = fetch.updateFlash();
	else ret = fetch.updateFS();
	if (ret) { msg.text = "Success"; bot.sendMessage(msg); Flag = RESTART; return; }
	else { msg.text = "Error"; bot.sendMessage(msg); } log_d("%u",uxTaskGetStackHighWaterMark2(NULL));
	timer_restart(timer_sab); timer_start(timer_sab);
	vTaskResume(sendTaskHandle);
	dWrite(PIN_LED, LED_OFF); dWrite(PIN_LED, LED_OFF);
}

void updateHandler(fb::Update& u) {
	if (u.isMessage() && u.message().from().id() == USER_ID) {
		if (Flag != RESEND_MSG) Flag = CHECK_MSG;
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

String get_info(bool ver) {
	uint32_t heap = ESP.getFreeHeap(); uint32_t sec = uS / 1000000; String str; str.reserve(256);
	str += "Connected to: "; str += ssid; str += "\nLocal IP: "; str += WiFi.localIP().toString(); str += "\nRSSI: "; str += WiFi.RSSI();
	str += "\nFree Heap: "; str += heap; str += "\nStack watermark:"; str += "\nmainTask "; str += uxTaskGetStackHighWaterMark2(NULL);
	str += "\nsendTask "; str += uxTaskGetStackHighWaterMark2(sendTaskHandle);
	str += "\ninterrupt_delta =  "; str += interrupt_delta;
	str += "\nlast_interrupt =  "; str += last_interrupt;
	str += "\nUptime: "; str += sec / 3600 / 24;  str += "d "; str += sec / 3600 % 24; str += "h "; str += sec / 60 % 60; str += "m "; str += sec % 60; str += 's';
	str += "\nUnix time: "; str += (timestamp_unix + ((uS - timestamp_sync) / 1000000));
	if (ver) { str += "\nCompiled: "; str += __DATE__; str += '\t'; str += __TIME__; str += '\n'; } log_d("%u", str.length());
	return str;
}

String create_hex_string(const byte* const& buf, const byte data_size) {
	String str("", data_size * 3 - 1);
	byte2hexstr(&str[0], buf, data_size);
	//DEBUGLN(str.length());
	return str; (void)1;
}

bool strtoB(const String& str, byte sub, byte*& buf, byte& data_len, const byte hexSizeMin) {
	unsigned str_len = str.length() - sub, hex_len = (str_len + 1) / 2; //DEBUGLN(hex_len);
	if (hex_len < hexSizeMin || hex_len > 255) return false;
	cch* ptr = str.c_str() + sub;
	free(buf);
	buf = (byte*)malloc(hex_len);
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

void byte2hexstr(char* text, const byte* buf, const byte data_size) {
	byte shift, nibble, num; size_t i = 0;
	for (;;) {
		for (shift = 4, num = buf[i];; shift = 0) {
			nibble = (num >> shift) & 0xF;
			nibble < 10 ? *text++ = nibble ^ 0x30 : *text++ = nibble + ('A' - 10);
			if (shift == 0) break;
		}//1185140 //1185054
		if(++i >= data_size) return;
		*text++ = ' ';
	}//*(text - 1) = '\0';
}
