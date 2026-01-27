#include "esp32_pir_tg_bot.h"
/*		INIT	*/
extern "C" void app_main() {
	main_init();//nvs_func();
	dWrite(PIN_LINE, 1);pinMode(PIN_LINE, PULLUP | OUTPUT_OPEN_DRAIN);//dWrite(PIN_PULLUP, 1); pinMode(PIN_PULLUP, OUTPUT); 
	AutoLed<PIN_LED> led; pinMode(PIN_LED,OUTPUT);
	QueueMsgHandle = xQueueCreateStatic(QUEUE_LEN, QUEUE_ITEM_SIZE, QueueMsgStorage, &xStaticQueue);
	loopTaskHandle = xTaskCreateStatic(mainTask, "main", sizeof(xMainStack), NULL, 9, xMainStack, &xMainTaskBuffer);
	vTaskSuspend(loopTaskHandle); //vTaskPrioritySet(NULL, 12);
	sendTaskHandle = xTaskCreateStatic(sendTask, "send", sizeof(xSendStack), NULL, 11, xSendStack, &xSendTaskBuffer);
#ifdef CONFIG_GENERIC_LINE
	adcReadTaskHandle = xTaskCreateStatic(adcReadTask, "adc", sizeof(xAdcReadStack), NULL, 2, xAdcReadStack, &xAdcReadBuffer);
#endif
	nvs_read_sets();
	read_credentials();
	init_adc_values();
	init_sets();
	sets.alarm ? alarm_on(false) : alarm_off(false);
	SPIFFS.begin();
#ifndef FIRST_BUILD
	assert(timer_pwr = esp_timer_init([](void* ) { dWrite(PIN_PWR_BUTTON, 1); } )); 
	pinMode(PIN_LED_D5, PULLUP | OUTPUT); pinMode(PIN_BUTTON, INPUT);//pinMode(PIN_RELAY, OUTPUT);
	dWrite(PIN_PWR_BUTTON, 1); pinMode(PIN_PWR_BUTTON, OUTPUT_OPEN_DRAIN);
#ifdef RELAY
	dWrite(PIN_RELAY, RELAY_STATE(sets.relay)); pinMode(PIN_RELAY, OUTPUT);
#endif
	WiFi.mode(WIFI_MODE_STA);
#else
	wifi_ap_init();
#endif
	wifi_server_init();
	if (!wifi_sta_init()) { /*WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);*/ /* if (!wifi_sta_init()) wifi_ap_init(); */ };
	configTzTime("MSK-3", "pool.ntp.org", "time.nist.gov"); 
	time_sync();//setenv("TZ", "MSK-3", 1); tzset();
	bot.attachUpdate(updateHandler);
	bot.skipUpdates(-2);
	bot.setPollMode(fb::Poll::Long, 30000);
	botSend.client.setHandshakeTimeout(5); bot.client.setHandshakeTimeout(15);
	send_alarm_time(Message("", CHAT_ID), bot, false);
	bot.sendMessage(Message(get_info(true), CHAT_ID));
	log_d("StackHighWaterMark: %u", uxTaskGetStackHighWaterMark2(NULL));
	vTaskResume(loopTaskHandle);
}

void mainTask(void*) {
	for (TickType_t lastTry = 0;; /* led_blink() */ ) {//CHANGED
		switch (Flag) {
		case RESEND_MSG: 
		if (xTaskGetTickCount() - lastTry > pdMS_TO_TICKS(15 * 60 * 1000)) {
			if(send_alarm_time()) Flag = CHECK_MSG; lastTry = xTaskGetTickCount(); log_i("%u", Flag);
		}
		case CHECK_MSG: 
			{ AutoLed <PIN_LED_D5> led;
			if (wifi_sta_init()) { delay(10); bot.tick(); } } 
			delay(FB_LONG_POLL_TOUT);	continue;
		case WIFI_DISCONNECT: time_sync(); WiFi.disconnect();
			ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(60 * 60 * 1000));
		case WIFI_RECON: 
			if (wifi_sta_init()) { bot.tickManual(); } continue;
		case WIFI_INIT: WiFi.begin(Auth->ssid, Auth->pass);
			delete Auth; Auth = nullptr;
			Flag = CHECK_MSG; continue;
		case RESTART: bot.tickManual(); esp_restart(); return;
		default: vTaskDelay(1);  ESP_LOGI("main",""); Flag = CHECK_MSG;
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
				case DOOR_CLOSE: msg.text = "DOOR_CLOSE";  break;
				case DOOR_OPEN: msg.text = "DOOR_OPEN";  break;
				case LINE_LOW:  msg.text = "LINE_LOW";  break;
				case LINE_HIGH: msg.text = "LINE_HIGH"; break;
				case RELAY_0: msg.text = "RELAY_OFF"; break;
				case RELAY_1: msg.text = "RELAY_ON"; break;
				case ok: msg.text = "OK";
					if(event.counter) { CHECK_(timer_alarm(timer_sab, TIMER_SABOTAGE)); }
					break; //goto _OK;
				default: msg.text = "0x"; msg.text += String(event.status, HEX); break;
				}
				msg.text.concat('\t'); msg.text.concat(event.delta);
				//_OK: //if (event.counter > 1) { msg.text.concat("\t%\t"); msg.text.concat(event.counter); }
				vTaskDelayUntil(&tick, pdMS_TO_TICKS(1000)); tick = xTaskGetTickCount();
				log_i("%s", msg.text.c_str()); //if(!dRead(PIN_BUTTON)) goto save;
				if (botSend.sendMessage(msg)) {
					if(Flag > CHECK_MSG) resumeTask(); log_v(""); 
					continue;
				}
				else {
					log_w("try again"); delay(2000);
					if (botSend.sendMessage(msg)) { tick = xTaskGetTickCount(); continue; }
				} log_d("%u", ESP.getFreeHeap());
			}
			else if (!event_id) event_id = WiFi.onEvent(onWiFiConnected);
save:		if (event.status != ok) { log_i("save");
				appendFile(ALARM_PATH, timestamp_unix + ((uS - time_sync_unix) / 1000000));
				if(Flag != RESEND_MSG) resumeTask(RESEND_MSG); 
			}
		}//vTaskGetInfo();
	}
}

void adcReadTask(void*) { 
	const adc_digi_output_data_t *p; tgMsg_t out {};
	for(uint32_t ret_num, val, i, last_sw = 0;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        //TickType_t now =  xTaskGetTickCount(); ESP_LOGD("TIME", "%lu", pdTICKS_TO_MS(now - last)); last = now;
            esp_err_t ret = adc_continuous_read(adc_handle, adc_buf, BUF_ADC_SIZE, &ret_num, 0);
            if (ret == ESP_OK)  {
                //ESP_LOGW("TASK", "ret = %x, ret_num = %"PRIu32" bytes", ret, ret_num);
                ret_num /= SOC_ADC_DIGI_RESULT_BYTES;
                for (i = 0, val = 0; i < ret_num; i++) { 
                    p = &reinterpret_cast<decltype(p)>(adc_buf)[i];// __unused uint32_t chan_num = EXAMPLE_ADC_GET_CHANNEL(p);
                    val += ADC_GET_DATA(p);
                }
                val /= SAMPLE_BUF;
				uint32_t volt = val * 3300 / 4095; 
				ESP_LOGD("adc", "Voltage: %lu\tValue: %lu", volt, val);
			if(1) {
				if(val > gerkon_open_high) { 
					if(out.status == LINE_HIGH) continue; 
					out.status = LINE_HIGH;
				}
				else if(val > gerkon_close_high) { 
					if(!sets.alarm || out.status == DOOR_OPEN) continue; 
					out.status = DOOR_OPEN; 
				} 
				else if(val > gerkon_close_low ) { 
					if(!sets.alarm || out.status == DOOR_CLOSE) continue;  
					out.status = DOOR_CLOSE; 
				} 
				else if(val > adc_button_low) { //TODO
					//if(++out.counter == ADC_TASK_FREQ);
					TickType_t now = xTaskGetTickCount();
					if(now - last_sw > pdMS_TO_TICKS(DEF_SWITCH_DELAY)) {
						last_sw = now;
						const int new_state = !dRead(PIN_RELAY); sets.relay = RELAY_STATE(new_state);
						dWrite(PIN_RELAY, new_state);
						if(!sets.alarm) continue;
						out.status = RELAY_STATE(new_state) ? RELAY_1 : RELAY_0;
					}else continue;
				}
				else { if(out.status == LINE_LOW) continue; out.status = LINE_LOW; } //val < adc_button_low
				out.delta = val; out.counter = 0;
				xQueueSend(QueueMsgHandle, &out, 0);
			}	
            }
            else if (ret == ESP_ERR_TIMEOUT) { /* ESP_LOGW("ERR", "TIMEOUT"); */ }
            else { ESP_LOGW("ERR", "err = 0x%02X", ret); }
	
    } //ESP_ERROR_CHECK(adc_continuous_stop(adc_handle)); ESP_ERROR_CHECK(adc_continuous_deinit(adc_handle));
}

bool conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) {
    vTaskNotifyGiveFromISR(adcReadTaskHandle, NULL);
    return false;
}

void time_sync(byte wait_sec) {
	DEBUG("Time sync "); if (!WiFi.isConnected()) return;
	time_t temp = 0;
	for (;; --wait_sec) {
		time(&temp);
		if (temp > 0x40000000) break;
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
	tgMsg_t tmp; uint32_t delta = time - last_interrupt; 
	last_interrupt = time; //interrupt_delta = delta; 
	if (delta < 2400000 /*&& delta > 10000*/) {
		if (delta < 10000) { disableInterrupt(PIN_LINE); esp_timer_start_once(timer_intr, 1000 * 300); } 
		tmp.status = ALARM; tmp.delta = (uint16_t)(delta / 1000);
	}
	else if (last_state == ok) return;
	else { tmp.status = ok; tmp.counter = (last_state > LINE_LOW) ? 1 : 0;
	} //isr_log_d("%u", tmp.status);
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
		file.read((byte*)&timestamp, sizeof(time_t)); DEBUG(timestamp); DEBUG(' ');//"%H:%M:%S %d.%m.%y"
		/* timeinfo = localtime(&timestamp);  //localtime_r() do same
		strftime(&ptr[offset], 9, "%H:%M:%S", timeinfo);
		offset += 8;
		if (timeinfo->tm_yday != tm_yday_last) {
			strftime(&ptr[offset], 10, " %d.%m.%y", timeinfo);
			offset += 9;
			tm_yday_last = timeinfo->tm_yday;
		}
		ptr[offset++] = '\n'; //free(timeinfo);
		 */str += (uint32_t)timestamp; str += "\n";
	}DEBUGLN(); //log_d("%u", ESP.getFreeHeap());
	//ptr[offset - 1] = '\0';
	//reinterpret_cast<uint32_t*>(&str)[2] = offset; //incapsulation hack //_ptr.len
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
		DEBUGLN(" written");
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
		DEBUGLN(" written");
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
	DEBUGLN(" deleted");
	return true;
}
/*		WIFI	*/
bool wifi_sta_init(uint32_t time) {
	if (WiFi.isConnected() == false) {
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
	/* __weak_symbol  */extern AsyncWebServer server;
	server.onNotFound([](AsyncWebServerRequest* request) {
		DEBUGLN("[" + request->client()->remoteIP().toString() + "] HTTP GET request of " + request->url());
		request->send(404, "text/plain", "Not found");
		});
	server.on("/connect", HTTP_GET, [](AsyncWebServerRequest* request) { String str;
		if(Auth) { str = "Connecting to:\n"; str += "SSID = ["; str += (Auth->ssid); str += "]\n"; str += "PASS = ["; str += Auth->pass; str += "]\n";
			resumeTask(WIFI_INIT); }
		else { str = "Auth empty"; } 
		request->send(200, "text/plain", str); 
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
		auto response = request->beginResponse(SPIFFS, CHANGE_FAILE_PATH, "text/html");
		response->addHeader(asyncsrv::T_Content_Encoding, "gzip");
		request->send(response);
		});
	server.on(CHANGE_AUTH, HTTP_POST, onConfigRequest);
#if defined RELAY
	server.on(RELAY_ON, HTTP_GET, [](AsyncWebServerRequest* request) {
		const int new_state = !dRead(PIN_RELAY); sets.relay = RELAY_STATE(new_state);
		dWrite(PIN_RELAY, new_state);
		request->send(200, "text/plain", RELAY_STATE(new_state) ? "RELAY ON" : "RELAY OFF");
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
	if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
		WiFi.removeEvent(event_id); event_id = 0; log_d("");
		resumeTask(RESEND_MSG);
	} else { log_d( "%u", event); }
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
	log += "Done. Go to ""/connect"; DEBUGLN(log);
	request->send(200, "text/plain", log);
	//resumeTask(WIFI_INIT);
}
/*		TELEGRAM	*/
void handleMessage(fb::Update& u) {
	Message msg("",u.message().chat().id()); DEBUGLN(u.message().text());
	switch (u.message().text().hash()) {
	case SH("/connect"):
		Flag = CHECK_MSG; msg.text = "Stay connected"; break;
	case SH("/disconnect"):
		Flag = WIFI_DISCONNECT; msg.text = "Disconnecting..."; break;
	case SH("/alarm"):
		alarm_on(); msg.text = "Alarm on"; break;
	case SH("/alarm0"):
		alarm_off(); msg.text = "Alarm off"; break;	
	case SH("/info"):
		msg.text = std::move(get_info()); break;
	case SH("/task_list"):
		get_task_list(msg.text); break;
	case SH("/restart"):
		bot.reboot(); Flag = RESTART; msg.text = "Restarting..."; break;
	case SH("/time_sync"):
		time_sync_unix = uS;
		timestamp_unix = u[tg_apih::date].toInt(); msg.text = "Done"; break;
	case SH("/send_alarm"):
		send_alarm_time(std::move(msg), bot); return;
	case SH("/clear_alarm"):
		msg.text = deleteFile(ALARM_PATH) ? "Done" : "No file"; break;
	case SH("/valid"):
		msg.text = (int)img_state(true); break;
	case SH("/invalid"): esp_ota_mark_app_invalid_rollback_and_reboot(); return;
	/* case SH("/pir_reset"):
		 pir_reset(); msg.text = "Done"; break; */
	case SH("/bot_kill"): {memset((void*)&bot, 0, sizeof(botSend)); } break;
	//case SH("/die"): Flag = PANIC; msg.text = "/die"; break;
		//case SH("/ota_invalidate"):
		//msg.text = (int)esp_ota_invalidate_inactive_ota_data_slot(); break;
		//case SH("/timer_count"): {uint64_t count = 0; gptimer_get_raw_count(timer_sab, &count);msg.text = String(count /= 1000); } break;
		//case SH("/nvs_erase"): nvs_wifi_erase(); break;
#if defined RELAY
	case SH(RELAY_ON): {
		const int new_state = !dRead(PIN_RELAY); sets.relay = RELAY_STATE(new_state);
		dWrite(PIN_RELAY, new_state); //msg.text = "RELAY ON"; 
	} break;
#endif
#ifndef FIRST_BUILD
	case SH("/pwr"):
	if(!esp_timer_start_once(timer_pwr, 100 * 1000))
	{ dWrite(PIN_PWR_BUTTON, 0); msg.text = "pwr_btn"; } break;
	case SH("/pwr_push"):
	if(!esp_timer_start_once(timer_pwr, 10 * 1000 * 1000))
	{ dWrite(PIN_PWR_BUTTON, 0); msg.text = "pwr_btn"; msg.text += " push"; } break;
	case SH("/pwr_up"):
		dWrite(PIN_PWR_BUTTON, 1); msg.text = "pwr_btn"; msg.text += " release"; break;
#endif
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
		/* if (u.message().text().startsWith(BLE_SET)) {
			if (strtoB(u.message().text(), ble_data, ble_data_size, sizeof(BLE_SET))) {
				create_hex_string(msg.text, ble_data, ble_data_size);
			}
			else msg.text = "Wrong format";
		} 
		else if (u.message().text().startsWith(PIN_GEN)) {
			uint32_t pin = generate_pin(u.message().text().str() + sizeof(PIN_PASS_SET)//-1
			,u.message().text().length(), pin_pass);
			msg.text = pin;
		} 
		 else if (u.message().text().startsWith(PIN_PASS_SET)) {
			if(u.message().text().length() > sizeof(PIN_PASS_SET) + 42) { msg.text = "too much size";} 
			else {
				pin_pass = u.message().text().str() + sizeof(PIN_PASS_SET); //with space
				msg.text = "password changed to: ";  msg.text += pin_pass;//sizeof(Text) 
				msg.text += '\n'; msg.text += "pin_pass_len = "; msg.text += pin_pass.length();
			}
		} 
		else*/ { msg.text = "Unknown"; }
	}
#else 
	default: {
		if(!update_adc_sets(u.message().text()._str, msg.text)) goto unknown;
		else { unknown: msg.text = "Unknown";} 
		}
	}
#endif
	DEBUGLN(msg.text);
	bot.sendMessage(msg);
}

void handleDocument(fb::Update& u) {
	switch (u.message()[tg_apih::caption].hash()) {
	case SH("/fw"): otaBegin(u, &Fetcher::updateFlash); break;
	case SH("/filesystem"): otaBegin(u, &Fetcher::updateFS); break;
	default: bot.sendMessage(Message("Unknown", u.message().chat().id()));
	}
}

bool update_adc_sets(cch* data,  String& text) {
	if(!strncmp(data,"/mode_", sizeof("/mode_")-1)) {
		data += sizeof("/mode_")-1;
		byte proxima = 0xFF, adc = 0xFF;
		if(data[0] == '1') { proxima = 1;}
		else if(data[0] == '0') { proxima = 0; } 
		if(data[1] == '1') { adc = 1; }
		else if(data[1] == '0') { adc = 0; } 
		if(proxima == 0xFF || adc == 0xFF) {
			goto error;
		} else {
			text = "Proxima "; text += proxima ? "ON": "OFF";
			text += "\nADC_Line "; text += adc ? "ON": "OFF";
			sets.proxima = proxima, sets.adc_line = adc;
			init_sets(); //return true;
		};
	} 
	else if(!strncmp(data,"/gerkon_", sizeof("/gerkon_")-1)) {
		unsigned res; data += sizeof("/gerkon_")-1;
		if(!strncmp(data, "open_", sizeof("open_")-1)) {
			data += sizeof("open_")-1;
			if((res = atoi(data) > 0)) {
				gerkon_open_default = res;
				text = "gerkon_open_default = "; text += res;
			} else goto error;
		}
		else if(!strncmp(data, "close_", sizeof("close_")-1)) {
			data += sizeof("close_")-1;
			if((res = atoi(data) > 0)) {
				gerkon_close_default = res;
				text = "gerkon_close_default = "; text += res;
			} else goto error;
		} else if(!strncmp(data, "button_", sizeof("button_")-1)) {
			data += sizeof("button_")-1;
			if((res = atoi(data) > 0)) {
				gerkon_button_default = res;
				text = "gerkon_button_default = "; 
			} else goto error;
		} else return false;
		text += res;
		init_adc_values();
	}
	return true;
error: text = "WRONG INPUT"; return true;
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
			msg.text += "esp_err = 0x"; msg.text += String(update_error, HEX); update_error = 0;} 
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
		memcpy(config.sta.ssid, DEFAULT_SSID, sizeof(DEFAULT_SSID));
		memcpy(config.sta.password, DEFAULT_PASS, sizeof(DEFAULT_PASS));
		esp_wifi_set_config(WIFI_IF_STA, &config);
	}
}

void nvs_read_sets() {
	nvsApi nvs;
    if(nvs.begin(NVS_WIFI_SPACE, NVS_READWRITE) == ESP_OK) {
        auto ret = nvs_get_u32(nvs, NVS_KEY, reinterpret_cast<uint32_t*>(&sets)); //reinterpret_cast<uint32_t*>(&sets)
        if (ret == ESP_OK) {
            //if (crc8_le(0, (byte*)&sets, 3) == sets.crc) {
				ESP_LOGI(TAG, "Alarm %u, relay %u, proxima  %u, adc_line %u", 
					sets.alarm, sets.relay, sets.proxima, sets.adc_line);
	            //sets.alarm ? alarm_on(false) : alarm_off(false);
                return;
            //} else { ESP_LOGW(TAG, "sets.crc = 0x%02X", sets.crc); };
        } else { CHECK_(ret); }
    }
	sets.alarm = 1; sets.proxima = 1, sets.adc_line = 1;//alarm_on(false);
	nvs_write_sets(nvs);
}

void nvs_write_sets(nvsApi nvs) {
	//sets.crc = crc8_le(0, (byte*)&sets, 3); log_d("%u, %02X", sets.alarm, sets.crc);
    esp_err_t ret = nvs_set_u32(nvs, NVS_KEY, reinterpret_cast<uint32_t&>(sets));
	if(ret == ESP_OK) { ret = nvs_commit(nvs); return; };
    CHECK_(ret);
}

void get_task_list(String& str) {
	auto num = uxTaskGetNumberOfTasks(); log_d("GetNumberOfTasks = %u", num);
	if (!str.reserve(num * 35)) return;
	char* const ptr = str.begin();
	vTaskList(ptr);
	reinterpret_cast<uint32_t*>(&str)[2] = strlen(ptr);
}

String get_info(bool ver) {
	uint32_t heap = ESP.getFreeHeap(); uint32_t sec = uS / 1000000;
	String str; str.reserve(300);
	str += "Connected to: "; str += WiFi.SSID(); str += "\nLocal IP: "; str += WiFi.localIP().toString(); str += "\nRSSI: "; str += WiFi.RSSI();
	str += "\nFree Heap: "; str += heap; str += "\nStack watermark:"; str += "\nmain "; str += uxTaskGetStackHighWaterMark2(NULL);
	str += "\nsend "; str += uxTaskGetStackHighWaterMark2(sendTaskHandle);
#ifdef CONFIG_GENERIC_LINE
	str += "\nadc "; str += uxTaskGetStackHighWaterMark2(adcReadTaskHandle);
#endif
	//str += "\ninterrupt_delta =  "; str += interrupt_delta;
	str += "\nSettings 0x"; str += String(reinterpret_cast<uint32_t&>(sets), HEX);
	if(last_interrupt != 0xFFFFFF) { str += "\nlast_interrupt: "; str += last_interrupt; }
	str += "\nUptime: "; str += sec / 3600 / 24;  str += "d "; str += sec / 3600 % 24; str += "h "; str += sec / 60 % 60; str += "m "; str += sec % 60; str += "s";
	str += "\nUnix time: "; str += (timestamp_unix + ((uS - time_sync_unix) / 1000000ul));
	if (ver) {
		if (img_state(false) == ESP_OTA_IMG_PENDING_VERIFY) { str += "ESP_OTA_IMG_PENDING_VERIFY"; }
		str += ("\nCompiled: " __TIMESTAMP__ "\n");
	} log_d("%u", str.length());
	return str;
}

void init_sets() {
#ifdef CONFIG_PROXIMA_PIR
	if(sets.proxima && !timer_sab) { 
		assert(timer_sab = timer_init(TIMER_SABOTAGE, sabotage_timer));
		assert(timer_intr = esp_timer_init([](void*) { enableInterrupt(PIN_LINE); } ));
		attachInterrupt(PIN_LINE, &isr_handler, GPIO_INTR_NEGEDGE);
	} else if(!sets.proxima && timer_sab){ 
		detachInterrupt(PIN_LINE);
		CHECK_(esp_timer_delete(timer_intr));
		CHECK_(gptimer_stop(timer_sab));
		CHECK_(gptimer_disable(timer_sab));
		CHECK_(gptimer_del_timer(timer_sab)); 
		timer_sab = NULL;
	}
#endif
#ifdef CONFIG_GENERIC_LINE
	if(sets.adc_line && !adc_handle) {
		adc_channel_t channel[] = { PIN_ADC_LINE }; int size = sizeof(channel)/sizeof(adc_channel_t);
		assert(adc_handle = continuous_adc_init(conv_done_cb, channel, size, BUF_ADC_SIZE)); 
		//ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
		gpio_config_t conf = { BIT(PIN_ADC_LINE) | BIT(PIN_ADC_PULLUP), GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE };
	    gpio_config(&conf);
		vTaskResume(adcReadTaskHandle);
	}
	else if (!sets.adc_line && adc_handle) {
		vTaskSuspend(adcReadTaskHandle);
		CHECK_(adc_continuous_stop(adc_handle)); 
		CHECK_(adc_continuous_deinit(adc_handle));
		adc_handle = NULL;
	}
#endif
}

void alarm_on(bool write) {
#ifdef CONFIG_PROXIMA_PIR
	if(sets.proxima) {  enableInterrupt(PIN_LINE); timer_start_impl(); ESP_LOGI(TAG, "ALARM_ON"); }
#endif
#ifdef CONFIG_GENERIC_LINE
	if(sets.adc_line) { /* nothing */  } else {/* nothing */ }
#endif
#if defined CONFIG_GENERIC_LINE || defined CONFIG_GENERIC_LINE
	sets.alarm = 1;
	if (write) {  nvs_write_sets(); } 
#endif
}

void alarm_off(bool write) {
#ifdef CONFIG_PROXIMA_PIR
	if(sets.proxima) { 
		disableInterrupt(PIN_LINE); timer_stop_impl();  ESP_LOGI(TAG, "ALARM_OFF");
	}
#endif
#ifdef CONFIG_GENERIC_LINE
	if(sets.adc_line) { /* nothing */  } else {/* nothing */ }
#endif
#if defined CONFIG_GENERIC_LINE || defined CONFIG_GENERIC_LINE
	sets.alarm = 0;
	if (write) {  nvs_write_sets(); } 
#endif
}

void create_hex_string(String& str, cbyte* buf, cbyte data_size) {
	const size_t str_size = data_size * 3;
	if (!str.reserve(str_size)) return; //log_d("%u", str.isSSO());
	char* ptr = str.begin(); if(!ptr) { log_d("NULL"); return; }
	byte *field = reinterpret_cast<byte*>(&str) + (sizeof(String) -1); static_assert(sizeof(String) == 16);
	if(*field & 0x80) { *field = (str_size | 0x80); }// SSO
	else reinterpret_cast<uint32_t*>(&str)[2] = str_size;
	for (byte i = 0, shift, nibble, num;;*ptr++ = ' ') {
		for (shift = 4, num = buf[i];; shift = 0) {
			nibble = (num >> shift) & 0xF;
			*ptr++ = nibble < 10 ? nibble ^ 0x30 : nibble + ('A' - 10);
			if (shift == 0) break;
		} 
		if (++i >= data_size) break;
	}*ptr = '\0';
	log_d("str.length: %u, data_size: %u", str.length(), data_size);
}

bool strtoB(const String& str, byte*& buf, byte & data_size, byte sub, bool heap) { 
	size_t str_len = str.length() - sub, hex_len = (str_len + 1) / 2; log_d("hex_len = %u", hex_len);
	if (hex_len < 4 || hex_len > 255) return false;
	byte i = 0; cch* ptr = str.c_str() + sub;
	byte* _buf = (byte*)realloc(buf, hex_len); if (_buf == NULL) return false; buf = _buf;
        for (byte shift = 0, result = 0;; ++ptr) {
                switch (*ptr) {
                case '0'... '9':
                    result <<= shift;
                    result |= (*ptr ^ 0x30); break;
                case 'A'... 'F':
                    result <<= shift;
                    result |= *ptr - 55; break;
                case 'a' ...'f':
                    result <<= shift;
                    result |= *ptr - 87; break;
                case '\0': buf[i] = result; data_size = ++i;return true;
                default: if(!shift) continue;
                goto rdy;
            }
            if (shift) {
rdy:            buf[i] = result;
                if (++i >= hex_len) break;
                result = 0; shift = 0;
            } else { shift = 4;};
    }//
    data_size = i;
	return realloc(buf, i);
}

adc_continuous_handle_t continuous_adc_init(adc_continuous_callback_t cb, const adc_channel_t *channel, uint8_t channel_num, uint16_t buf_size) {
    adc_continuous_handle_t handle = NULL;
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = buf_size,
        .conv_frame_size = buf_size,//SOC_ADC_DIGI_DATA_BYTES_PER_CONV
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));
    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {};
    for (int i = 0; i < channel_num; i++) {
        adc_pattern[i].atten = ADC_ATTEN_DB_12;
        adc_pattern[i].channel = channel[i] & 0x7;
        adc_pattern[i].unit = ADC_UNIT_1;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
        ESP_LOGD(TAG, "adc_pattern[%d].atten is :%" PRIx8, i, adc_pattern[i].atten);
        ESP_LOGD(TAG, "adc_pattern[%d].channel is :%" PRIx8, i, adc_pattern[i].channel);
        ESP_LOGD(TAG, "adc_pattern[%d].unit is :%" PRIx8, i, adc_pattern[i].unit);
    }///*!< F_sample = F_digi_con / 2 / interval. F_digi_con = 5M for now. 30 <= interval <= 4095 */
    adc_continuous_config_t dig_cfg = {
		.pattern_num = channel_num,
        .adc_pattern = adc_pattern,
        .sample_freq_hz = SOC_ADC_SAMPLE_FREQ_THRES_LOW,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_OUTPUT_TYPE,
    };
    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));
    if(cb) {
        adc_continuous_evt_cbs_t cbs = { .on_conv_done = cb};
        ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL));
    }
    ESP_ERROR_CHECK(adc_continuous_start(handle));
    return handle;
}


