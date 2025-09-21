#pragma once

#define USER_ID ""
#ifdef DEBUG_ENABLE
#define BOT_TOKEN ""
#define CHAT_ID USER_ID
#define SSID_HIDDEN 0
#define STA_INIT_DELAY 1000
#else
#ifdef FIRST_BUILD
#define BOT_TOKEN ""
#define CHAT_ID ""
#else
#define BOT_TOKEN ""
#define CHAT_ID USER_ID
#endif
#define SSID_HIDDEN 1
#define STA_INIT_DELAY 100
#endif // DEBUG_ENABLE

#define DEFAULT_SSID ""
#define DEFAULT_PASS ""
#define AP_SSID ""
#define AP_PASS ""
#define AP_WIFI_CHANNEL 13
#define CHANGE_AUTH ""
#define NVS_NAMESPACE "nvs.net80211"
#define NVS_KEY_SSID "sta.ssid"
#define NVS_KEY_PASS "sta.pswd"

#define BLE_SET "/ble_set"
#define PIN_GEN "/pin_gen"
#define PIN_PASS_SET "/pin_pass_set"
#define RELAY_ON "/relay_on"
#define RELAY_OFF "relay_off"
#define SSID_PATH "/ssid.txt"
#define PASS_PATH "/pass.txt"
#define ALARM_PATH "/alarm.bin"

