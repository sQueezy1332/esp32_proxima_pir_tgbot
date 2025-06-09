#include "esp32_pir_tg_bot.h"
/*		INIT	*/
extern "C" void app_main() {
	main_init();
	QueueMsgHandle = xQueueCreateStatic(QUEUE_LEN, QUEUE_ITEM_SIZE, &QueueMsgStorage[0], &pxStaticQueue);
	pinMode(PIN_LINE, INPUT_PULLUP); pinMode(PIN_PULLUP, OUTPUT); dWrite(PIN_PULLUP, 1);
	pinMode(PIN_LED, OUTPUT); //pinMode(PIN_RELAY, OUTPUT);
	AutoLed<PIN_LED> led;
#ifdef ESP32C3_LUATOS
	pinMode(PIN_LED_D5, OUTPUT); dWrite(PIN_LED_D5, LED_OFF);
#endif
	_CHECK(timer_init(TIMER_SABOTAGE, timer_sab, sabotage_check, 0, 0));
	attachInterrupt(PIN_LINE, &isr_handler, GPIO_INTR_NEGEDGE);
	if (!SPIFFS.begin()) DEBUGLN("\nAn error has occurred while mounting SPIFFS");
	WiFi.mode(WIFI_MODE_APSTA);
	wifi_server_init();
	read_credentials();
	wifi_sta_init();
#ifdef DEBUG_ENABLE
	WiFi.printDiag(Serial); //log_d("sizeof(QueueMsgStorage) %u ", sizeof(QueueMsgStorage));
#endif 
	//setenv("TZ", "MSK-3", 1); tzset();
	configTzTime("MSK-3", "pool.ntp.org", "time.nist.gov"); time_sync();
	bot_upd.attachUpdate(updateHandler);
	bot_upd.skipUpdates(-2);
	bot_upd.setPollMode(fb::Poll::Long, 30000);
#ifndef DEBUG_ENABLE
	send_alarm_time(Message("", CHAT_ID), false);
#endif // DEBUG_ENABLE
	loopTaskHandle = xTaskCreateStatic(mainTask, "main", sizeof(xMainStack), NULL, 10, xMainStack, &xMainTaskBuffer);
	sendTaskHandle = xTaskCreateStatic(sendTask, "send", sizeof(xSendStack), NULL, 11, xSendStack, &xSendTaskBuffer);
	bot.sendMessage(Message(get_info(true), CHAT_ID));
	alarm_on(); log_d("StackHighWaterMark: %u", uxTaskGetStackHighWaterMark2(NULL));
}

void mainTask(void*) {
	for (TickType_t tick = 0;;) {
		switch (Flag) {
		case RESEND_MSG:
			if (!send_alarm_time(Message("", CHAT_ID))) {
				log_i("%u", Flag); delay(15 * 60 * 1000);
			} else Flag = CHECK_MSG;
		case CHECK_MSG:
			if (wifi_sta_init()) { bot_upd.tick(); } //log_v("");
			vTaskDelayUntil(&tick, pdMS_TO_TICKS(300)); continue;
		case WIFI_DISCONNECT:
			time_sync(); WiFi.disconnect();
			vTaskDelayUntil(&tick, pdMS_TO_TICKS(60 * 60 * 1000));
			if (wifi_sta_init()) { bot.tickManual(); } continue;
		case WIFI_INIT:
			WiFi.begin(ssid, pass); Flag = CHECK_MSG; continue;
		case RESTART: bot_upd.tickManual(); yield(); esp_restart();
		default:Flag = CHECK_MSG;
		}
	}
}

void sendTask(void*) {
	Message msg("", CHAT_ID); tgMsg_t event; //sizeof(FastBot2);
	for (TickType_t tick = 0;;) {
		if (xQueueReceive(QueueMsgHandle, &event, portMAX_DELAY) == pdPASS) {
			AutoLed<PIN_LED> led;
			if (wifi_sta_init()) {
				switch (event.status) {
				case ALARM: msg.text = "ALARM"; break;
				case LINE_HIGH: msg.text = "LINE_HIGH"; break;
				case LINE_LOW:  msg.text = "LINE_LOW";  break;
				default: msg.text = "OK"; goto _OK;
				}
				msg.text.concat('\t'); msg.text.concat(event.delta);
			_OK://if (event.counter > 1) { msg.text.concat("\t%\t"); msg.text.concat(event.counter); }
				vTaskDelayUntil(&tick, pdMS_TO_TICKS(1000));
				tick = xTaskGetTickCount(); log_i("%s", msg.text.c_str());
				if (bot.sendMessage(msg)) {
					Flag = CHECK_MSG; log_v("");
					continue;
				}
				else {
					log_w("try again"); delay(2000);
					if (bot.sendMessage(msg)) { tick = xTaskGetTickCount(); continue; }
				}
				log_d("%u", ESP.getFreeHeap());
			}
			else if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
			if (event.status != ok) {
				appendFile(ALARM_PATH, timestamp_unix + ((uS - time_sync_unix) / 1000000));
				Flag = RESEND_MSG;
			}
		}//vTaskGetInfo();
	}
}

void time_sync(uint32_t wait_sec) {
	DEBUG("Time sync "); if (!WiFi.isConnected()) return;
	time_t temp = 0; wait_sec *= 10;
	for (;; --wait_sec) {
		time(&temp);
		if (temp > 1000000000) break;
		if (wait_sec == 0) {
			DEBUGLN(" failed!"); return;
		}
		delay(100); DEBUG("*");
	}
	time_sync_unix = uS; DEBUGLN(" success");
	timestamp_unix = temp;
	log_i("%u , timer %u", temp, wait_sec);
}
/*		INTERRUPTS		*/
static void IRAM_ATTR isr_handler(/*void**/) {
	if (lineRead) return;
	uint64_t time = uS;
	timer_restart(timer_sab); tgMsg_t tmp;
	uint32_t delta = time - last_interrupt;
	last_interrupt = time; interrupt_delta = delta;
	if (delta < 2400000 /*&& delta > 10000*/)
#ifdef DEBUG_ENABLE
	{ last_state = tmp.status = ALARM; }
	else if (last_state == ok) return;
	else { last_state = tmp.status = ok; }
	isr_log_d("%u", last_state);
#else
	{ tmp.status = ALARM; }
	else return;
#endif // DEBUG_ENABLE
	tmp.delta = (uint16_t)(delta / 1000);
	xQueueSendFromISR(QueueMsgHandle, &tmp, nullptr);//sizeof(tgMsg_t)
}

static bool IRAM_ATTR sabotage_check(gptimer_handle_t tmr, const gptimer_alarm_event_data_t* edata, void* user_ctx) {
	uint64_t delta = edata->count_value / 1000; 
	tgMsg_t tmp{
		.status = lineRead ? LINE_HIGH : LINE_LOW,
		.delta = (uint16_t)(delta > __UINT16_MAX__ ? __UINT16_MAX__ : delta)
	};
#ifdef DEBUG_ENABLE
	last_state = tmp.status;
#endif // DEBUG_ENABLE
	xQueueSendFromISR(QueueMsgHandle, &tmp, nullptr); 
	return false;
}
/*		FILE SYSTEM	*/
bool send_alarm_time(Message&& msg, bool no_file) {
	DEBUG("Reading file: "); DEBUGLN(ALARM_PATH); //sizeof(Message);108
	String& str = msg.text; File file = SPIFFS.open(ALARM_PATH, FILE_READ);
	if (!file || file.isDirectory() || !file.available()) {
		DEBUGLN(" failed to open file for reading");
		if (no_file) { str = "No file"; bot.sendMessage(msg); }
		return true;
	}
	const uint32_t file_size = file.size(), count = file_size / sizeof(_time_t), str_len = 18 * count, heap = ESP.getFreeHeap();
	char* ptr; size_t offset; time_t timestamp = 0; tm timeinfo{ 0 }; int tm_yday_last = 0; //memset(&timeinfo, 0, sizeof(timeinfo));
	log_i("file_size %u, count %u, str_len %u, HEAP %u", file_size, count, str_len, heap);
	if (!str.reserve(str_len) /*|| heap - 5000 < str_len*/) {
		str += "file_size \n"; str += file_size; str += "count \n";
		str += count; str += "str_len \n"; str += str_len; str += "HEAP \n"; str += heap;
		goto try_send;
	}ptr = str.begin();
	for (offset = 0; offset < str_len && file.available();) {
		file.read((byte*)&timestamp, sizeof(_time_t));
		localtime_r(&timestamp, &timeinfo); DEBUG(timestamp); DEBUG(' ');//"%H:%M:%S %d.%m.%y"
		strftime(&ptr[offset], 9, "%H:%M:%S", &timeinfo);
		offset += 8;
		if (timeinfo.tm_yday != tm_yday_last) {
			strftime(&ptr[offset], 10, " %d.%m.%y", &timeinfo);
			offset += 9;
			tm_yday_last = timeinfo.tm_yday;
		}
		ptr[offset++] = '\n'; //free(timeinfo);
	}DEBUGLN(); //log_d("%u", ESP.getFreeHeap());
	ptr[offset - 1] = '\0';
	reinterpret_cast<uint32_t*>(&str)[2] = offset; //incapsulation hack //_ptr.len
try_send: DEBUGLN(str);
	if (wifi_sta_init()) {
		if (bot.sendMessage(msg)) {
			if (event_id) { WiFi.removeEvent(event_id); event_id = 0; }
		}
		return true;
	}
	else if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
	return false;

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
	if (!file) { DEBUGLN(" failed to open file for writing"); }
	else if (file.print(Content)) {
		DEBUGLN(" file written");
		return true;
	}
	else DEBUGLN(" write failed");
	return false;
}

bool appendFile(cch* path, _time_t value) {
	DEBUG("Append file: "); DEBUG(path);
	fs::File file = SPIFFS.open(path, FILE_APPEND);
	if (!file) { DEBUGLN(" failed to open file for writing"); }
	else if (file.write((byte*)&value, sizeof(_time_t))) {
		DEBUGLN(" file written"); return true;
	}
	else DEBUGLN(" write failed");
	return false;
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
bool wifi_sta_init(uint32_t wait_sec) {
	if (!WiFi.isConnected()) {
		WiFi.begin(ssid, pass); wl_status_t status; log_i("Wait connection %u sec", wait_sec);
		for (wait_sec *= 10; (status = WiFi.status()) != WL_CONNECTED; ) {
			if (--wait_sec == 0) {
				log_w("Not connected");
				return false;
			}delay(100); DEBUG(status); DEBUG(' ');
		} DEBUGLN(status);
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
		WiFi.removeEvent(event_id); event_id = 0; log_d("");
		resumeTask(RESEND_MSG);
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
	case SH("/info"):
		msg.text = std::move(get_info());
		break;
	case SH("/task_list"):
		get_task_list(msg.text); break;
	case SH("/restart"):
		msg.text = "ESP restarting...";
		bot.reboot(); Flag = RESTART; break;
	case SH("/time_sync"): {
		time_sync_unix = uS;
		msg.text = u[tg_apih::date];
		timestamp_unix = msg.text.toInt(); }break;
	case SH("/send_alarm"):
		send_alarm_time(std::move(msg)); return;
	case SH("/clear_alarm"):
		msg.text = deleteFile(ALARM_PATH) ? "Done" : "No file";
		break;
	case SH("/valid"):
		msg.text = (int)img_state(true); break;
	case SH("/invalid"):
		esp_ota_mark_app_invalid_rollback_and_reboot(); return;
		/*case SH("/ota_invalidate"):
			msg.text = (int)esp_ota_invalidate_inactive_ota_data_slot(); break;*/
	case SH("/timer_count"): {uint64_t count = 0;
		gptimer_get_raw_count(timer_sab, &count); count /= 1000;
		msg.text = String(count); } break;
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
	bot_upd.sendMessage(msg);
	}

void handleDocument(fb::Update& u) {
	switch (u.message()[tg_apih::caption].hash()) {
	case SH("/fw"): otaBegin(u, &Fetcher::updateFlash); break;
	case SH("/filesystem"): otaBegin(u, &Fetcher::updateFS); break;
	default: bot.sendMessage(Message("Unknown", u.message().chat().id()));
	}
}

void otaBegin(fb::Update& u, bool(Fetcher::* upd)()) {
	AutoLed<PIN_LED> led; alarm_off();
	vTaskSuspend(sendTaskHandle);
	auto ptr = esp_ota_get_next_update_partition(NULL);
	Message msg("OTA begin\nPartition: ", u.message().chat().id());
	msg.text += ptr->label; msg.text += "\nsize: "; msg.text += ptr->size;
	bot_upd.sendMessage(msg);
	Fetcher fetch = bot_upd.downloadFile(u.message().document().id());
	if (fetch) {
		if ((fetch.*upd)()) { msg.text = "Success\nRestarting..."; Flag = RESTART; }
		else { msg.text = "Error"; }
	}
	else { msg.text = "Download error"; }
	log_i("%s", msg.text.c_str());
	bot_upd.sendMessage(msg);
	vTaskResume(sendTaskHandle);
	alarm_on(); log_d("StackHighWaterMark: %u", uxTaskGetStackHighWaterMark2(NULL));
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
	if (str.reserve(num * 35)) {
		char* const ptr = str.begin();
		vTaskList(ptr);
		reinterpret_cast<uint32_t*>(&str)[2] = strlen(ptr);
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
	str += "\nUnix time: "; str += (timestamp_unix + ((uS - time_sync_unix) / 1000000ul));
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
	if (!str.reserve(str_size)) return; char* ptr = str.begin();
	reinterpret_cast<uint32_t*>(&str)[2] = str_size - 1;
	for (;;) {
		for (shift = 4, num = buf[i];; shift = 0) {
			nibble = (num >> shift) & 0xF;
			nibble < 10 ? *ptr = nibble ^ 0x30 : *ptr = nibble + ('A' - 10);
			++ptr;
			if (shift == 0) break;
		}
		if (++i >= data_size) { *ptr = '\0'; break; }
		*ptr++ = ' ';
	} log_d("%u", str.length());
}

bool strtoB(const String& str, byte sub, byte*& buf, byte& data_len) {
	size_t str_len = str.length() - sub, hex_len = (str_len + 1) / 2; log_d("%u", hex_len);
	if (hex_len < 6 || hex_len > 255) return false;
	byte i = 0; cch* ptr = str.c_str() + sub;
	free(buf); buf = (byte*)malloc(hex_len);
	if (buf == NULL) return false;
	for (byte ready = 0, result = 0; *ptr; ++ptr) {
		switch (*ptr) {
		case'0'... '9':
			result |= (*ptr ^ 0x30); break;
		case 'A'... 'F':
			result |= *ptr - 55; break;
		case 'a'...'f':
			result |= *ptr - 87; break;
		default:
			if (ready) goto rdy;
			continue;
		}
		if (ready) {
		rdy:
			buf[i] = result;
			if (++i == hex_len) break;
			result = 0; ready = 0;
			continue;
		}
		if (result & 0xf) result <<= 4;
		ready = 1;
	}
	return realloc(buf, (data_len = i));
}
