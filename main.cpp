#include "esp32_pir_tg_bot.h"
extern int main();
extern "C" void app_main() {
	main_init();
	setup();
	QueueStatHandle = xQueueCreateStatic(QUEUE_SIZE, QUEUE_ITEM_SIZE, &QueueStatStorage[0], &pxStaticQueue);
	loopTaskHandle = xTaskCreateStatic(mainTask, "main", sizeof(xMainStack), NULL, 10, xMainStack, &xMainTaskBuffer);
	sendTaskHandle = xTaskCreateStatic(sendTask, "send", sizeof(xSendStack), NULL, 11, xSendStack, &xSendTaskBuffer);
	alarm_on(); timer_start(timer_sab);
	log_d("StackHighWaterMark = %u", uxTaskGetStackHighWaterMark2(NULL));
}

void mainTask(void*) {
	for (TickType_t tick = 0;;) {
		switch (Flag) {
		case RESEND_MSG:
			if (xTaskGetTickCount() - tick > pdMS_TO_TICKS(30 * 60 * 1000)) {
				send_alarm_time(Message("", CHAT_ID)); tick = xTaskGetTickCount(); log_i("%u", Flag);
			}
		case CHECK_MSG:
			if (wifi_sta_init()) { bot.tick(); } //log_v("");
			delay(100); continue;
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
		case RESTART: bot.tickManual(); yield(); esp_restart();
		default:Flag = CHECK_MSG; continue;
		}
		vTaskDelayUntil(&tick, pdMS_TO_TICKS(60 * 60 * 1000));
	}
}

void sendTask(void*) {
	Message msg("", CHAT_ID); tgMessage_t event;
	for (;;delay(900)) {
		if (xQueueReceive(QueueStatHandle, &event, portMAX_DELAY) == pdPASS) {
			AutoLed<PIN_LED> led; 
			if (wifi_sta_init()) {
				while (bot.isPolling()) { delay(1000); }
				switch (event.status) {
					//case ok: msg.text = "OK"; break;
					//case ALARM: msg.text = "ALARM"; break;
				case LINE_HIGH: msg.text = "LINE_HIGH"; break;
				case LINE_LOW:  msg.text = "LINE_LOW"; break;
				default: msg.text = "ALARM"; //msg.text = (uint8_t)tmp.status; log_d("default");
				}
				//if (tmp.status != ok) 
				{ msg.text += '\t'; msg.text += (event.delta); }
				log_i("%s", msg.text.c_str());
				if (bot.sendMessage(msg)) {
					Flag = CHECK_MSG; log_v("");
					continue;
				}
			}
			else if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
			//if (tmp.status != ok)
			{
				appendFile(ALARM_PATH, timestamp_unix + ((uS - time_sync_unix) / 1000000));
				Flag = RESEND_MSG;
			}
		}//vTaskGetInfo();
	}
}
/*		INIT	*/
void setup() {
	pinMode(PIN_LINE, INPUT_PULLUP); //pinMode(PIN_PULLUP, OUTPUT); dWrite(PIN_PULLUP, 1);
	pinMode(PIN_LED, OUTPUT); AutoLed<PIN_LED> led;//pinMode(PIN_RELAY, OUTPUT);
#ifdef ESP32C3_LUATOS
	pinMode(PIN_LED_D5, OUTPUT); dWrite(PIN_LED_D5, LED_OFF);
#endif
	_CHECK(timer_init(TIMER_SABOTAGE, timer_sab, &sabotage_check, 0, 0, 0));
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
	bot.setToken(F(BOT_TOKEN)); 
	bot.attachUpdate(updateHandler); //bot.setPollMode(Poll::Long, 20000);
	bot.skipUpdates();
	bot.setPollMode(fb::Poll::Long, 30000);
	bot.sendMessage(Message(get_info(true), CHAT_ID));
	send_alarm_time(Message("",CHAT_ID), false);
	//if (SPIFFS.exists(String(OTA_FILE_PATH) + ".gz"))ESP_LOGD("OTA_FILE_PATH", OTA_FILE_PATH);
	//ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN,"" , "HUY");
	String str("0123456789ABCD");
	char* ptr;
again:
	ptr = str.begin();
	DEBUG("adress = 0x"); DEBUGLN(reinterpret_cast<uint32_t>(ptr), HEX);
	for (size_t i = 0; i < 15; i++) {
		DEBUG(ptr[i]); DEBUG('_');
	}
	DEBUGF("length() = %u", str.length());
	DEBUGF("%02X", ptr[15]);
	if (str.length() >= 15) {
		DEBUGLN((reinterpret_cast<uint32_t*>(ptr))[1]);
		DEBUGLN((reinterpret_cast<uint32_t*>(ptr))[2]);
		return;
	}
	str += 'E'; goto again;
	delay(100);
}

void time_sync(byte wait_sec) {
	DEBUG("Time sync ");
	time_t temp;
	for (TickType_t ticker = 0;; --wait_sec) {
		time(&temp);
		if (temp > 1000000000) break;
		if (wait_sec == 0) {
			DEBUGLN(" failed!");
			return;
		}
		vTaskDelayUntil(&ticker, pdMS_TO_TICKS(1000));
	}
	time_sync_unix = uS;
	timestamp_unix = temp;
	log_i("%u", timestamp_unix);
}
/*		INTERRUPTS		*/
static void IRAM_ATTR ISR() {
	uint64_t time = uS; uint32_t delta = time - last_interrupt;
	//static uint64_t last_alarm = 0; static byte alarm_count = 0;
	last_interrupt = time; interrupt_delta = delta; tgMessage_t tmp; //sizeof(tgMessage_t)
	timer_restart(timer_sab);
	if (alarm_state && delta < 2400000 && delta > 1000) {
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
	auto delta = (uS - last_interrupt) / 1000;
	tgMessage_t tmp{
		.status = lineRead ? LINE_HIGH : LINE_LOW,
		.delta = (uint16_t)(delta > __UINT16_MAX__ ? __UINT16_MAX__ : delta)
	}; //prev_alarm = tmp.status;
	xQueueSendFromISR(QueueStatHandle, &tmp, nullptr);
	return false;
}
/*		FILE SYSTEM	*/
void send_alarm_time(Message && msg, bool no_file) {
	DEBUG("Reading file: "); DEBUGLN(ALARM_PATH); //sizeof(Message);108
	fs::File file = SPIFFS.open(ALARM_PATH, FILE_READ);
	if (!file || file.isDirectory() || !file.available()) {
		DEBUGLN(" failed to open file for reading");
		if (no_file) { msg.text = "No file"; bot.sendMessage(msg); } 
		return;
	}
	const uint32_t file_size = file.size(), count = file_size / sizeof(_time_t), str_len = (18 * count) - 1, heap = ESP.getFreeHeap();
	log_i("file_size %u, count %u, str_len %u, HEAP %u", file_size, count, str_len, heap);
	if (!msg.text.reserve(str_len) || heap - 5000 < str_len) {
		msg.text = "No enough memory for the operation.";
		bot.sendMessage(msg); return;
	}
	char* ptr = msg.text.begin(); time_t timestamp = 0; struct tm timeinfo;
	for (size_t offset = 0; offset < str_len, file.available();) {
		file.read((byte*)&timestamp, sizeof(_time_t));
		localtime_r(&timestamp, &timeinfo); DEBUGLN(timestamp); //"%H:%M:%S %d.%m.%y"
		strftime(&ptr[offset], 18, "%H:%M:%S %d.%m.%y", &timeinfo);
		offset += 18;
		ptr[offset - 1] = '\n';
	}
	ptr[str_len] = '\0';
	(reinterpret_cast<uint32_t*>(&ptr))[2] = str_len; //incapsulation hack //_ptr.len
	DEBUGLN(msg.text);
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
	}
	else {
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
/*		WIFI	*/
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
#if	AP_WIFI_CHANNEL > 11
	_CHECK(esp_wifi_set_country_code("CN", false));
#endif
	//WiFi.softAP(AP_SSID, AP_PASS, AP_WIFI_CHANNEL, SSID_HIDDEN);
	//WiFi.setTxPower(WIFI_POWER_20dBm); DEBUGLN(WiFi.getTxPower()); //WIFI_POWER_20dBm = 80,// 20dBm
	//WiFi.softAPbandwidth(WIFI_BW_HT20);
	//DEBUGLN("\nAP running"); DEBUGLN(AP_SSID); DEBUGLN(AP_PASS); DEBUG("My IP address: "); DEBUGLN(WiFi.softAPIP());
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
	ota::server_init(server, ota_progress);
	log_v("server.begin()");
	server.begin(); // Start server
}

void onWiFiConnected(arduino_event_id_t event) {
	if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
		WiFi.removeEvent(event_id); event_id = 0;
		resumeTask();
	}
}

void onConfigRequest(AsyncWebServerRequest* request) {
	auto pSSID = request->getParam(0), pPASS = request->getParam(1);
	bool wrongSSID = pSSID->value().length() == 0, wrongPASS = pPASS->value().length() < 8;
	if (wrongSSID && wrongPASS) {
		request->send(200, "text/plain", "WRONG INPUT");
		return;
	}
	String log; log.reserve(64);
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
void handleMessage(fb::Update& u) {
	Message msg; msg.chatID = u.message().chat().id(); DEBUGLN(u.message().text());
	switch (u.message().text().hash()) {
	case SH("/connect"):
		msg.text = "ESP will stay connected";
		Flag = CHECK_MSG; break;
	case SH("/disconnect"):
		msg.text = "Disconnecting...";
		Flag = WIFI_DISCONNECT; break;
	case SH("/alarm_on"):
		msg.text = "Alarm on";
		alarm_on(); break;
	case SH("/alarm_off"):
		msg.text = "Alarm off";
		alarm_off(); break;
	case SH("/get_info"):
		msg.text = std::move(get_info());
		break;
	case SH("/get_tasks"):
		get_task_list(msg.text); break;
	case SH("/restart"):
		msg.text = "ESP restarting...";
		bot.reboot(); Flag = RESTART; break;
	case SH("/send_alarm"):
		send_alarm_time(std::move(msg)); return;
	case SH("/clear_alarm"):
		msg.text = deleteFile(ALARM_PATH) ? "Done" : "No file";
		break;
	case SH("/valid"):
		msg.text = (int)img_state(true); break;
	case SH("/invalid"):
		esp_ota_mark_app_invalid_rollback_and_reboot(); return;
	case SH("/ota_invalidate"):
		msg.text = (int)esp_ota_invalidate_inactive_ota_data_slot(); break;
		//case SH("/suicide")://suicide_func(); xTaskCreate(suicide_func2, "HUY", 2048, NULL, 6, NULL); break;
#if defined RELAY
	case SH(RELAY_ON):
		dWrite(PIN_RELAY, HIGH);
		bot.sendMessage(Message("RELAY ON", chat)); break;
	case SH(RELAY_OFF):
		dWrite(PIN_RELAY, LOW);
		bot.sendMessage(Message("RELAY OFF", chat)); break;
#endif
#ifndef NO_BLE
	case SH("/ble"): {
		auto ret = ble_advertising(ble_data, ble_data_size);
		if (ret == ESP_OK) msg.text = "BLE data sended";
		else if (ret == -1)  msg.text = "BLE data empty";
		else msg.text = String(ret, HEX);
	} break;
	case SH("/ble_clear"):
		free(ble_data); ble_data = nullptr; ble_data_size = 0;
		msg.text = "Done"; break;
	case SH("/ble_deinit"):
		break;
	default: {
		if (u.message().text().startsWith(BLE_SET)) {
			if (strtoB(u.message().text(), sizeof(BLE_SET), ble_data, ble_data_size)) {
				create_hex_string(msg.text, ble_data, ble_data_size);
			}
			else msg.text = "Wrong format";
		}
		else { msg.text = "Unknown"; }
	}
#else
	default: msg.text = "Unknown";
#endif 
	}DEBUGLN(msg.text);
	bot.sendMessage(msg);
}

void handleDocument(fb::Update& u) {
	switch (u.message()[tg_apih::caption].hash()) {
	case SH("/fw"): otaBegin(u, &Fetcher::updateFlash); break;
	case SH("/filesystem"): otaBegin(u, &Fetcher::updateFS); break;
	default: bot.sendMessage(Message("Unknown", u.message().chat().id()));
	}
}

void otaBegin(fb::Update& u, bool(Fetcher::* fun)()) {
	dWrite(PIN_LED, LED_ON);
	vTaskSuspend(sendTaskHandle); timer_stop(timer_sab);
	Message msg("OTA begin", u.message().chat().id()); bool no_err;
	bot.sendMessage(msg);
	Fetcher fetch = bot.downloadFile(u.message().document().id());
	if (!fetch) { msg.text = "Download error"; bot.sendMessage(msg); log_e("Download error"); }
	no_err = (fetch.*fun)();
	if (no_err) { msg.text = "Success"; bot.sendMessage(msg); Flag = RESTART; return; }
	else { msg.text = "Error"; bot.sendMessage(msg); }
	log_d("StackHighWaterMark = %u", uxTaskGetStackHighWaterMark2(NULL));
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
esp_err_t ble_advertising(cbyte* ble_data, cbyte ble_data_length, uint32_t time_ms) {
	if (ble_data == nullptr || ble_data_length == 0) return -1; log_d("%u", ESP.getFreeHeap());
	return ESP_OK;
}
#endif
/*		MISC		*/
void read_credentials() {
	if (!readFile(SSID_PATH, ssid) || ssid.length() == 0
		|| !readFile(PASS_PATH, pass) || pass.length() < 8) {
		log_e("\nERROR READ WIFI LOGIN"); //delay(2000);
		ssid = DEFAULT_SSID;
		pass = DEFAULT_PASS;
	} DEBUGLN(ssid); DEBUGLN(pass);
}

void get_task_list(String& str) {
	auto num = uxTaskGetNumberOfTasks(); log_d("GetNumberOfTasks = %u", num);
	char* ptr = str.begin();
	if (str.reserve(num * 30)) {
		vTaskList(ptr);
		((uint32_t*)&ptr)[2] = strlen(ptr);
	}
}

String get_info(bool ver) {
	uint32_t heap = ESP.getFreeHeap(); uint32_t sec = uS / 1000000;
	String str; str.reserve(255);
	str += "Connected to: "; str += ssid; str += "\nLocal IP: "; str += WiFi.localIP().toString(); str += "\nRSSI: "; str += WiFi.RSSI();
	str += "\nFree Heap: "; str += heap; str += "\nStack watermark:"; str += "\nmainTask "; str += uxTaskGetStackHighWaterMark2(NULL);
	str += "\nsendTask "; str += uxTaskGetStackHighWaterMark2(sendTaskHandle);
	str += "\ninterrupt_delta =  "; str += interrupt_delta;
	str += "\nlast_interrupt =  "; str += last_interrupt;
	str += "\nUptime: "; str += sec / 3600 / 24;  str += "d "; str += sec / 3600 % 24; str += "h "; str += sec / 60 % 60; str += "m "; str += sec % 60; str += 's';
	str += "\nUnix time: "; str += (timestamp_unix + ((uS - time_sync_unix) / 1000000));
	if (ver) {
		str += "\nCompiled: "; str += __DATE__; str += '\t'; str += __TIME__; str += '\n';
		if (img_state(false) == ESP_OTA_IMG_PENDING_VERIFY) {
			str += "ESP_OTA_IMG_PENDING_VERIFY";
		}
	} log_d("%u", str.length());
	return str;
}

void create_hex_string(String& str, cbyte* const& buf, cbyte data_size) {
	byte shift, nibble, num; size_t i = 0, str_size = data_size * 3;
	char* ptr = str.begin(); if (!str.reserve(str_size)) return;
	((uint32_t*)&ptr)[2] = str_size - 1;
	for (;;) {
		for (shift = 4, num = buf[i];; shift = 0) {
			nibble = (num >> shift) & 0xF;
			nibble < 10 ? *ptr = nibble ^ 0x30 : *ptr = nibble + ('A' - 10);
			ptr++;
			if (shift == 0) break;
		}
		if (++i >= data_size) break;
		*ptr++ = ' ';
	} *ptr = '\0';//DEBUGLN(str.length());
}

bool strtoB(const String& str, byte sub, byte*& buf, byte& data_len, byte hexSizeMin) {
	unsigned str_len = str.length() - sub, hex_len = (str_len + 1) / 2; //DEBUGLN(hex_len);
	if (hex_len < hexSizeMin || hex_len > 255) return false;
	cch* ptr = str.c_str() + sub;
	free(buf);
	if ((buf = (byte*)malloc(hex_len)) == NULL) return false;
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
