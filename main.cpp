#include "esp32_pir_tg_bot.h"
/*		INIT	*/

extern "C" void app_main() {
	main_init();//nvs_func();
	dWrite(PIN_LINE, 1);pinMode(PIN_LINE,INPUT_PULLUP | OUTPUT_OPEN_DRAIN);//dWrite(PIN_PULLUP, 1); pinMode(PIN_PULLUP, OUTPUT); 
	pinMode(PIN_LED_D5, PULLUP | OUTPUT); pinMode(PIN_BUTTON, INPUT);//pinMode(PIN_RELAY, OUTPUT);
	AutoLed<PIN_LED> led; pinMode(PIN_LED,OUTPUT);
	QueueMsgHandle = xQueueCreateStatic(QUEUE_LEN, QUEUE_ITEM_SIZE, &QueueMsgStorage[0], &xStaticQueue);
	//timerSabotage = xTimerCreateStatic("sab", pdMS_TO_TICKS(TIMER_SABOTAGE +50), pdFALSE, NULL, sabotageCallback, &xTimerSabBuffer); 
	CHECK_(timer_init(TIMER_SABOTAGE, timer_sab, sabotage_timer, false));
	timerInterrupt = xTimerCreateStatic("intr", pdMS_TO_TICKS(200), pdFALSE, NULL, [](TimerHandle_t xTimer) {enableInterrupt(PIN_LINE);}, &xTimerIntrBuffer);
	attachInterrupt(PIN_LINE, &isr_handler, GPIO_INTR_NEGEDGE);
	nvs_read_sets();
	SPIFFS.begin();
#ifndef FIRST_BUILD
	WiFi.mode(WIFI_MODE_STA);
#else
	wifi_ap_init();
	wifi_server_init();
#endif
	read_credentials();
	if (!wifi_sta_init()) { /*WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);*/ /* if (!wifi_sta_init()) wifi_ap_init(); */ };
	configTzTime("MSK-3", "pool.ntp.org", "time.nist.gov"); 
	time_sync();//setenv("TZ", "MSK-3", 1); tzset();
	bot.attachUpdate(updateHandler);
	bot.skipUpdates(-2);
	bot.setPollMode(fb::Poll::Long, 30000);
	botSend.client.setHandshakeTimeout(5);bot.client.setHandshakeTimeout(10);
	send_alarm_time(Message("", CHAT_ID), bot, false);
	loopTaskHandle = xTaskCreateStatic(mainTask, "main", sizeof(xMainStack), NULL, 9, xMainStack, &xMainTaskBuffer);
	sendTaskHandle = xTaskCreateStatic(sendTask, "send", sizeof(xSendStack), NULL, 11, xSendStack, &xSendTaskBuffer);
	bot.sendMessage(Message(get_info(true), CHAT_ID));
	log_d("StackHighWaterMark: %u", uxTaskGetStackHighWaterMark2(NULL));
	//byte key_open[] { 0x01,0xAB, 0xCD, 0xEF }; byte key_open2[] { std::byteswap({ 0x01,0xAB, 0xCD, 0xEF }); };
	
}


void mainTask(void*) {
	for (TickType_t lastTry = 0;; led_blink()) {//CHANGED
		switch (Flag) {
		case RESEND_MSG: 
		if(xTaskGetTickCount() - lastTry > pdMS_TO_TICKS(15 * 60 * 1000)) {
			if(send_alarm_time()) Flag = CHECK_MSG; lastTry = xTaskGetTickCount(); log_i("%u", Flag);
		}
		case CHECK_MSG:
			if (wifi_sta_init()) { bot.tick(); } //log_v("");
			delay(300); continue;
		case WIFI_DISCONNECT: time_sync(); WiFi.disconnect();
			ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(60 * 60 * 1000));
		case WIFI_RECON: if (wifi_sta_init()) { bot.tickManual(); } continue;
		case WIFI_INIT: WiFi.begin(Auth->ssid, Auth->pass);
			delete Auth; Auth = nullptr;
			Flag = CHECK_MSG; continue;
		case RESTART: yield(); bot.tickManual(); esp_restart();
		default: delay(1000); Flag = CHECK_MSG;
		}
	}
}

void sendTask(void*) {
	Message msg("", CHAT_ID); tgMsg_t event; 
	for (TickType_t tick = 0;;) {
		if (xQueueReceive(QueueMsgHandle, &event, portMAX_DELAY) == pdPASS) {
			AutoLed<PIN_LED> led; 
			if (wifi_sta_init()) {
				switch (event.status) {
				case ALARM: msg.text = "ALARM"; break;
				case LINE_HIGH: msg.text = "LINE_HIGH"; break;
				case LINE_LOW:  msg.text = "LINE_LOW";  break;
				default: msg.text = "OK"; if(event.counter) timer_alarm(TIMER_SABOTAGE, timer_sab); goto _OK;
				}
				msg.text.concat('\t'); msg.text.concat(event.delta);
				_OK: //if (event.counter > 1) { msg.text.concat("\t%\t"); msg.text.concat(event.counter); }
				vTaskDelayUntil(&tick, pdMS_TO_TICKS(1000)); tick = xTaskGetTickCount();
				log_i("%s", msg.text.c_str()); if(!dRead(PIN_BUTTON)) goto save;
				if (botSend.sendMessage(msg)) {
					if(Flag > CHECK_MSG) resumeTask(); log_v(""); 
					continue;
				}
				else {
					log_w("try again"); delay(2000);
					if (botSend.sendMessage(msg)) { tick = xTaskGetTickCount(); continue; }
				} log_d("%u", ESP.getFreeHeap());
			}
			else if (!event_id) event_id = WiFi.onEvent(onWiFiConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
save:		if (event.status != ok) { log_i("save");
				appendFile(ALARM_PATH, timestamp_unix + ((uS - time_sync_unix) / 1000000));
				if(Flag != RESEND_MSG) resumeTask(RESEND_MSG); 
			}
		}//vTaskGetInfo();
	}
}

void time_sync(byte wait_sec) {
	DEBUG("Time sync "); if (!WiFi.isConnected()) return;
	time_t temp = 0;
	for (;; --wait_sec) {
		time(&temp);
		if (temp > 1000000000) break;
		if (wait_sec == 0) {
			DEBUGLN(" failed!"); return;
		}
		delay(1000); DEBUG('*');
	}
	time_sync_unix = uS; DEBUGLN(" success");
	timestamp_unix = temp;
	log_i("%u , timer %u", temp, wait_sec);
}
/*		INTERRUPTS		*/
static void isr_handler(/*void**/) {
	if (lineRead) return;
	uint64_t time = uS;
	timer_restart_impl();
	uint32_t delta = time - last_interrupt; tgMsg_t tmp;
	last_interrupt = time; //interrupt_delta = delta; 
	if (delta < 2400000 /*&& delta > 10000*/) {
		if (delta < 10000) { disableInterrupt(PIN_LINE); xTimerStartFromISR(timerInterrupt, NULL); }
		tmp.status = ALARM; tmp.delta = (uint16_t)(delta / 1000);
	}
	else if (last_state == ok) return;
	else { tmp.status = ok; tmp.counter = (last_state > ALARM) ? 1 : 0;} //isr_log_d("%u", tmp.status);
	last_state = tmp.status;
	xQueueSendFromISR(QueueMsgHandle, &tmp, NULL);//sizeof(tgMsg_t)
}

static bool sabotage_timer(gptimer_handle_t tmr, const gptimer_alarm_event_data_t* edata, void* user_ctx) {
	uint32_t delta = edata->count_value / 1000;
	tgMsg_t tmp {
		.status = lineRead ? LINE_HIGH : LINE_LOW,
		.delta = (uint16_t)(delta > __UINT16_MAX__ ? __UINT16_MAX__ : delta)
	};
	last_state = tmp.status;
	xQueueSendFromISR(QueueMsgHandle, &tmp, nullptr);
	return false;
}

/*		FILE SYSTEM	*/
bool send_alarm_time(Message&& msg, FastBot2& _bot, bool no_file) {
	DEBUG("Reading file: "); DEBUG(ALARM_PATH); //sizeof(Message);108
	String& str = msg.text; File file = SPIFFS.open(ALARM_PATH, FILE_READ);
	if (!file || file.isDirectory() || !file.available()) {
		DEBUGLN(" failed to open file for reading");
		if (no_file) { str = "No file"; _bot.sendMessage(msg); }
		return true;
	}
	const uint32_t file_size = file.size(), count = file_size / sizeof(time_t), str_len = 18 * count, heap = ESP.getFreeHeap();
	__unused char* ptr; size_t offset; time_t timestamp = 0; tm * timeinfo; int tm_yday_last = 0;
	log_i("file_size %u, count %u, str_len %u, HEAP %u", file_size, count, str_len, heap);
	if (!str.reserve(str_len) /*|| heap - 5000 < str_len*/) {
		str += "file_size \n"; str += file_size; str += "count \n";
		str += count; str += "str_len \n"; str += str_len; str += "HEAP \n"; str += heap;
		goto try_send;
	}ptr = str.begin();
	for (offset = 0; offset < str_len && file.available();) {
		file.read((byte*)&timestamp, sizeof(time_t)); //DEBUG(timestamp); DEBUG(' ');//"%H:%M:%S %d.%m.%y"
		timeinfo = localtime(&timestamp);  //localtime_r() do same
		strftime(&ptr[offset], 9, "%H:%M:%S", timeinfo);
		offset += 8;
		if (timeinfo->tm_yday != tm_yday_last) {
			strftime(&ptr[offset], 10, " %d.%m.%y", timeinfo);
			offset += 9;
			tm_yday_last = timeinfo->tm_yday;
		}
		ptr[offset++] = '\n'; //free(timeinfo);
		//str += (uint32_t)timestamp; str += "\n";
	}DEBUGLN(); //log_d("%u", ESP.getFreeHeap());
	ptr[offset - 1] = '\0';
	reinterpret_cast<uint32_t*>(&str)[2] = offset; //incapsulation hack //_ptr.len
try_send: DEBUGLN(str);
	if (wifi_sta_init()) {
		if (_bot.sendMessage(msg)) {
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
	else {DEBUGLN(" write failed");}
	return false;
}

bool appendFile(cch* path, time_t value) {
	DEBUG("Append file: "); DEBUG(path);
	fs::File file = SPIFFS.open(path, FILE_APPEND);
	if (!file) { DEBUGLN(" failed to open file for writing"); }
	else if (file.write((byte*)&value, sizeof(time_t))) {
		DEBUGLN(" file written");
		return true;
	}
	else {DEBUGLN(" write failed");}
	return false;
}

bool deleteFile(cch* path) {
	DEBUG("Deleting file: "); DEBUG(path);
	if (!SPIFFS.remove(path)) {
		DEBUGLN(" delete failed");
		return false;
	}
	DEBUGLN(" file deleted");
	return true;
}
/*		WIFI	*/
bool wifi_sta_init(uint32_t time) {
	if (!WiFi.isConnected()) {
		if (!WiFi.STA.begin(true)) return false; wl_status_t status; log_i("Wait connection %u sec", time);
		for (; (status = WiFi.status()) != WL_CONNECTED; time--) {
			if (time == 0) { log_w("Not connected"); return false; }
			delay(STA_INIT_DELAY); DEBUG(status); DEBUG(' ');
		} DEBUGLN(status); log_d("%u", time);
	}
	return true;
}

void wifi_ap_init() {
	WiFi.mode(WIFI_MODE_APSTA);
#if	AP_WIFI_CHANNEL > 11
	CHECK_(esp_wifi_set_country_code("CN", false));
#endif
	WiFi.softAP(AP_SSID, AP_PASS, AP_WIFI_CHANNEL, SSID_HIDDEN);
	WiFi.setTxPower(WIFI_POWER_20dBm); DEBUGLN(WiFi.getTxPower()); //WIFI_POWER_20dBm = 80,// 20dBm
	WiFi.softAPbandwidth(WIFI_BW_HT20);
	DEBUGLN("\nAP running"); DEBUGLN(AP_SSID); DEBUGLN(AP_PASS); DEBUG("My IP address: "); DEBUGLN(WiFi.softAPIP());DEBUGLN();
}

void wifi_server_init() {
	__weak_symbol extern AsyncWebServer server;
	server.onNotFound([](AsyncWebServerRequest* request) {
		DEBUGLN("[" + request->client()->remoteIP().toString() + "] HTTP GET request of " + request->url());
		request->send(404, "text/plain", "Not found");
		});
	server.on("/connect", HTTP_GET, [](AsyncWebServerRequest* request) { String str;
		if(Auth == nullptr) { str = "Auth empty";}
		else { str = "Connecting to:\n"; str += "SSID = ["; str += (Auth->ssid); str += "]\n"; str += "PASS = ["; str += Auth->pass; str += "]\n"; } 
		request->send(200, "text/plain", str); resumeTask(WIFI_INIT);
		});
	server.on("/disconnect", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Disconnecting..."); resumeTask(WIFI_DISCONNECT);
		});
	server.on("/restart", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Esp restarting...");
		resumeTask(RESTART);
		});
	server.on("/alarm_on", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Alarm on"); alarm_on();
		});
	server.on("/alarm_off", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "Alarm off"); alarm_off();
		});
	server.on(CHANGE_AUTH, HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(SPIFFS, "/config.html", "text/html");
		});
	server.on(CHANGE_AUTH, HTTP_POST, onConfigRequest);

#if defined RELAY
	server.on(RELAY_ON, HTTP_GET, [](AsyncWebServerRequest* request) {
		dWrite(PIN_RELAY, HIGH); request->send(200, "text/plain", "RELAY ON");
		});
	server.on(RELAY_OFF, HTTP_GET, [](AsyncWebServerRequest* request) {
		dWrite(PIN_RELAY, LOW); request->send(200, "text/plain", "RELAY OFF");
		});
#endif
	ota::server_init(server
#ifdef DEBUG_ENABLE
		, ota_progress
#endif // DEBUG_ENABLE
	);
	server.begin(); log_v("server.begin()");
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
	if (wrongSSID || wrongPASS) {
		request->send(200, "text/plain", "WRONG INPUT"); return;
	}
	delete Auth;
	Auth = new auth_t{
		.ssid = pSSID->value(),
		.pass = pPASS->value()
	};//sizeof(auth_t)
	String log; log.reserve(128);
	//DEBUGF("POST[%s]: %s\n", pSSID->name().c_str(), pSSID->value().c_str(), pPASS->name().c_str(), pPASS->value().c_str());
	log += "SSID = ["; log += Auth->ssid; log += "]\n"; log += "PASS = ["; log += Auth->pass; log += "]\n";
	log += "Done. Connecting with new credentials"; DEBUGLN(log);
	request->send(200, "text/plain", log);
	resumeTask(WIFI_INIT);
}
/*		TELEGRAM	*/
void handleMessage(fb::Update& u) {
	Message msg; msg.chatID = u.message().chat().id(); DEBUGLN(u.message().text());
	switch (u.message().text().hash()) {
	case SH("/connect"):
		Flag = CHECK_MSG; msg.text = "Stay connected"; break;
	case SH("/disconnect"):
		Flag = WIFI_DISCONNECT; msg.text = "Disconnecting..."; break;
	case SH("/alarm_on"):
		alarm_on(); msg.text = "Alarm on"; break;
	case SH("/alarm_off"):
		alarm_off(); msg.text = "Alarm off"; break;
	case SH("/info"):
		msg.text = std::move(get_info()); break;
	case SH("/task_list"):
		get_task_list(msg.text); break;
	case SH("/restart"):
		bot.reboot(); Flag = RESTART; msg.text = "Restarting..."; break;
	case SH("/time_sync"):
		time_sync_unix = uS; msg.text = u[tg_apih::date];
		timestamp_unix = msg.text.toInt(); msg.text = "Done"; break;
	case SH("/send_alarm"):
		send_alarm_time(std::move(msg), bot); return;
	case SH("/clear_alarm"):
		msg.text = deleteFile(ALARM_PATH) ? "Done" : "No file"; break;
	case SH("/valid"):
		msg.text = (int)img_state(true); break;
	case SH("/invalid"): esp_ota_mark_app_invalid_rollback_and_reboot(); return;
	case SH("/pir_reset"):
		 pir_reset(); msg.text = "Done"; break;
	case SH("/bot_kill"): {memset((void*)&bot, 0x0, sizeof(botSend)); } break;
	//case SH("/die"): Flag = PANIC; msg.text = "/die"; break;
		//case SH("/ota_invalidate"):
		//msg.text = (int)esp_ota_invalidate_inactive_ota_data_slot(); break;
		//case SH("/timer_count"): {uint64_t count = 0; gptimer_get_raw_count(timer_sab, &count);msg.text = String(count /= 1000); } break;
		//case SH("/nvs_erase"): nvs_wifi_erase(); break;
#if defined RELAY
	case SH(RELAY_ON):
		dWrite(PIN_RELAY, HIGH); msg.text = "RELAY ON"; break;
	case SH(RELAY_OFF):
		dWrite(PIN_RELAY, LOW); msg.text = "RELAY OFF"; break;
#endif
	case SH("/pwr"):
		dWrite(PIN_PWR_BUTTON, 0); delay(100); dWrite(PIN_PWR_BUTTON, 1); msg.text = "pwr_btn"; break;
	case SH("/pwr_push"):
	if(xTimerStart(xTimerCreate("", pdMS_TO_TICKS(10'000), pdFALSE, NULL, 
	[](TimerHandle_t xTimer) { dWrite(PIN_PWR_BUTTON, 1); xTimerDelete(xTimer, 0); }), 0))
	{ dWrite(PIN_PWR_BUTTON, 0); msg.text = "pwr_btn"; msg.text += " push"; } break;
	case SH("/pwr_up"):
		dWrite(PIN_PWR_BUTTON, 1); msg.text = "pwr_btn"; msg.text += " release"; break;
#ifndef NO_BLE
	case SH("/ble_clear"):
		free(ble_data); ble_data = nullptr; ble_data_size = 0;
		msg.text = "Done"; break;
	case SH("/ble_deinit"): break;
	case SH("/ble"): {
		auto && ret = ble_advertising(ble_data, ble_data_size);
		if (ret == ESP_OK) msg.text = "BLE data sended";
		else if (ret == -1)  msg.text = "BLE data empty";
		else msg.text = String(ret, HEX);
	} break;
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

void otaBegin(fb::Update& u, bool(Fetcher::* updater)()) {
	AutoLed<PIN_LED> led; //vTaskSuspend(sendTaskHandle);
	bool temp = sets.alarm; if(temp) alarm_off(false);
	auto ptr = esp_ota_get_next_update_partition(NULL);
	Message msg("OTA begin\nPartition: ", u.message().chat().id());
	msg.text += ptr->label; msg.text += "\nsize: "; msg.text += ptr->size;
	bot.sendMessage(msg);
	Fetcher fetch = bot.downloadFile(u.message().document().id());
	if (fetch) {
		if ((fetch.*updater)()) { msg.text = "Success\nRestarting..."; Flag = RESTART; }
		else { msg.text = "Error = "; msg.text += Update.getError();msg.text += '\n';
			msg.text += "esp_err = "; msg.text += update_error; update_error = 0;} 
	}
	else { msg.text = "Download error"; }
	log_i("%s", msg.text.c_str());
	bot.sendMessage(msg);
	if(temp) alarm_on(false); vTaskResume(sendTaskHandle);
	log_d("StackHighWaterMark: %u", uxTaskGetStackHighWaterMark2(NULL));
}

void updateHandler(fb::Update& u) {
	if (u.isMessage() && u.message().from().id() == USER_ID) {
		if (Flag > CHECK_MSG) Flag = CHECK_MSG;
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
	wifi_config_t config {};  size_t ssid_l, pass_l;
	esp_wifi_get_config(WIFI_IF_STA, &config);log_d("%s\tpass: %s", config.sta.ssid,config.sta.password);
	ssid_l = strlen((char*)config.sta.ssid); pass_l = strlen((char*)config.sta.password);
	if (ssid_l < 1 || pass_l < 8) {
		log_w("ssid len %u, pass len %u", ssid_l, pass_l);
  		config.sta.threshold.rssi = -127;
  		config.sta.pmf_cfg.capable = true;
		memcpy(config.sta.ssid, DEFAULT_SSID, sizeof(DEFAULT_SSID));
		memcpy(config.sta.password, DEFAULT_PASS, sizeof(DEFAULT_PASS));
		esp_wifi_set_config(WIFI_IF_STA, &config);
	}
}

void nvs_read_sets() {
	
	auto ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvsHandle);
	ret = nvs_get_u32(nvsHandle, "sets", reinterpret_cast<uint32_t*>(&sets));
	if (ret != ESP_OK) { if (ret == ESP_ERR_NVS_NOT_FOUND) {} goto error; }
	if (crc8_le(0, (byte*)&sets, 3) != sets.crc) { log_w("crc err. Value = 0x%X", sets); goto error; }
	sets.alarm ? alarm_on(false) : alarm_off(false);
	nvs_close(nvsHandle); return;
error: alarm_on(false); nvs_write_sets(nvsHandle);
}

void nvs_write_sets(nvs_handle_t nvs) {
	nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
	sets.crc = crc8_le(0, (byte*)&sets, 3); log_d("%u, %u", sets.alarm, sets.crc);
	nvs_set_u32(nvs, "sets", reinterpret_cast<uint32_t&>(sets));
	nvs_close(nvs);
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
	String str; str.reserve(300);
	str += "Connected to: "; str += WiFi.SSID(); str += "\nLocal IP: "; str += WiFi.localIP().toString(); str += "\nRSSI: "; str += WiFi.RSSI();
	str += "\nFree Heap: "; str += heap; str += "\nStack watermark:"; str += "\nmain "; str += uxTaskGetStackHighWaterMark2(NULL);
	str += "\nsend "; str += uxTaskGetStackHighWaterMark2(sendTaskHandle);
	//str += "\ninterrupt_delta =  "; str += interrupt_delta;
	str += "\nSettings 0x"; str += String(reinterpret_cast<uint32_t&>(sets), HEX);
	str += "\nlast_interrupt: "; str += last_interrupt;
	str += "\nUptime: "; str += sec / 3600 / 24;  str += "d "; str += sec / 3600 % 24; str += "h "; str += sec / 60 % 60; str += "m "; str += sec % 60; str += "s";
	str += "\nUnix time: "; str += (timestamp_unix + ((uS - time_sync_unix) / 1000000ul));
	if (ver) {
		str += ("\nCompiled: " __DATE__ "\t" __TIME__ "\n");
		if (img_state(false) == ESP_OTA_IMG_PENDING_VERIFY) {
			str += "ESP_OTA_IMG_PENDING_VERIFY";
		}
	} log_d("%u", str.length());
	return str;
}

void alarm_on(bool write) {
	enableInterrupt(PIN_LINE);  timer_start_impl(); if (write) { sets.alarm = 1; nvs_write_sets(); } /*timer_restart(timer_sab); timer_start(timer_sab);*/
}

void alarm_off(bool write) {
	disableInterrupt(PIN_LINE); timer_stop_impl(); if (write) { sets.alarm = 0; nvs_write_sets(); } /*timer_stop(timer_sab);*/
}

void create_hex_string(String& str, cbyte* const& buf, cbyte data_size) {
	size_t i = 0, str_size = data_size * 3;
	if (!str.reserve(str_size)) return; char* ptr = str.begin();
	reinterpret_cast<uint32_t*>(&str)[2] = str_size;log_d("%u", str.length());
	for (byte shift, nibble, num;;) {
		for (shift = 4, num = buf[i];; shift = 0) {
			nibble = (num >> shift) & 0xF;
			*ptr = nibble < 10 ? nibble ^ 0x30 : nibble + ('A' - 10);
			++ptr;
			if (shift == 0) break;
		}
		if (++i >= data_size) break;
		*ptr++ = ' ';
	} *ptr = '\0';
}

bool strtoB(const String& str, byte sub, byte*& buf, byte& data_len) {
	size_t str_len = str.length() - sub, hex_len = (str_len + 1) / 2; log_d("hex_len = %u", hex_len);
	if (hex_len < 6 || hex_len > 255) return false;
	byte i = 0; cch* ptr = str.c_str() + sub;
	free(buf); buf = (byte*)malloc(hex_len);
	if (buf == NULL) return false;
	for (byte ready = 0, result = 0; *ptr; ++ptr) {
		switch (*ptr) {
		case '0'... '9':
			if (result & 0xf) result <<= 4;
			result |= (*ptr ^ 0x30); break;
		case 'A'... 'F':
			if (result & 0xf) result <<= 4;
			result |= *ptr - 55; break;
		case 'a'...'f':
			if (result & 0xf) result <<= 4;
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
		ready = 1;
	}
	return realloc(buf, (data_len = i)) != NULL;
}
