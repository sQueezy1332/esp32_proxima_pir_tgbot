file(GLOB_RECURSE srcs [FOLLOW_SYMLINKS] #"src/*.c" "src/*.cpp"
    "libs/FastBot2/core/*.cpp"
    "libs/FastBot2/core/types/*.cpp" 
    "libs/StringUtils/src/utils/*.cpp"
    "libs/StringUtils/src/utils/convert/*.cpp"
    "libs/ESPAsyncWebServer/*.cpp" 
    "libs/AsyncTCP/*.cpp")

idf_component_register( SRCS "${srcs}" SRCS "main.cpp" "esp32_pir_tg_bot.h" "ESP_MAIN.h" "credentials.h" 
    INCLUDE_DIRS
    "libs/FastBot2/" "libs/FastBot2/core" "libs/FastBot2/core/types"
    "libs/GSON/src" "libs/GSON/src/utils"
    "libs/StringUtils/src" "libs/StringUtils/src/utils" "libs/StringUtils/src/utils/convert/"
    "libs/GTL/src/"
    "libs/GyverHTTP/src" "libs/GyverHTTP/src/utils"
    "libs/AsyncTCP"
    "libs/ESPAsyncWebServer/"
    "libs/ESP_OTAserver/src")

spiffs_create_partition_image(spiffs ../data FLASH_IN_PROJECT)
set(ENV{ARDUINO_SKIP_TICK_CHECK} true)
