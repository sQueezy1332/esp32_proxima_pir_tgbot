#pragma once

#ifdef DEBUG_ENABLE
#define BOT_TOKEN ""
#define USER_ID ""
#define CHAT_ID USER_ID
#define SSID_HIDDEN 0
#else
#define BOT_TOKEN ""
#define USER_ID ""
#define CHAT_ID USER_ID// ""
#define SSID_HIDDEN 1
#endif // DEBUG_ENABLE
#define ESP32C3_LUATOS
#define DEFAULT_SSID ""
#define DEFAULT_PASS ""
#define AP_SSID ""
#define AP_PASS ""
#define WIFI_CHANNEL 13
#define CHANGE_AUTH ""

#define BLE_SET "/ble_set"
#define RELAY_ON "/relay_on"
#define RELAY_OFF "relay_off"
#define SSID_PATH "/ssid.txt"
#define PASS_PATH "/pass.txt"
#define ALARM_PATH "/alarm.txt"