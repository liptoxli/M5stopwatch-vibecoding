file(GLOB_RECURSE MY_SRCS
    "apps/*.c"
    "apps/*.cc"
    "apps/*.cpp"
    "assets/*.c"
    "assets/*.cc"
    "assets/*.cpp"
    "hal/*.c"
    "hal/*.cc"
    "hal/*.cpp"
)

list(FILTER MY_SRCS EXCLUDE REGEX "(^|.*/)apps/app_alarm_clock/.*")
list(FILTER MY_SRCS EXCLUDE REGEX "(^|.*/)apps/app_stopwatch/.*")
list(FILTER MY_SRCS EXCLUDE REGEX "(^|.*/)assets/images/alarm_icon\\.c$")
list(FILTER MY_SRCS EXCLUDE REGEX "(^|.*/)assets/images/icon_stopwatch\\.c$")

list(APPEND MY_SRCS
    "apps/app_codex/app_codex.cpp"
    "apps/app_codex/codex_quota_client.cpp"
    "apps/app_codex/view/view.cpp"
    "apps/app_setup/workers/wifi.cpp"
    "assets/images/icon_codex.c"
    "assets/images/codex_quota_left_track.c"
    "assets/images/codex_quota_left_fill.c"
    "assets/images/codex_quota_right_track.c"
    "assets/images/codex_quota_right_fill.c"
    "assets/images/codex_pet_idle0.c"
    "assets/images/codex_pet_idle1.c"
    "assets/images/codex_pet_idle2.c"
    "assets/images/codex_pet_idle3.c"
    "assets/images/codex_pet_idle4.c"
    "assets/images/codex_pet_idle5.c"
    "assets/images/codex_pet_blink0.c"
    "assets/images/codex_pet_blink1.c"
    "assets/images/codex_pet_touch0.c"
    "assets/images/codex_pet_touch1.c"
    "assets/images/codex_pet_touch2.c"
    "assets/images/codex_pet_touch3.c"
    "assets/images/codex_pet_processing0.c"
    "assets/images/codex_pet_processing1.c"
    "assets/images/codex_pet_processing2.c"
    "assets/images/codex_pet_processing3.c"
    "assets/images/codex_pet_msg_idle_0.c"
    "assets/images/codex_pet_msg_idle_1.c"
    "assets/images/codex_pet_msg_idle_2.c"
    "assets/images/codex_pet_msg_touch_0.c"
    "assets/images/codex_pet_msg_touch_1.c"
    "assets/images/codex_pet_msg_touch_2.c"
    "assets/images/codex_pet_msg_touch_3.c"
    "assets/images/codex_pet_msg_shake_0.c"
    "assets/images/codex_pet_msg_shake_1.c"
    "assets/images/codex_pet_msg_shake_2.c"
    "assets/images/codex_pet_msg_shake_3.c"
    "assets/images/codex_pet_msg_processing_0.c"
    "assets/images/codex_pet_msg_processing_1.c"
    "assets/images/codex_pet_msg_processing_2.c"
    "assets/images/codex_pet_msg_processing_3.c"
    "assets/images/codex_pet_msg_key_0.c"
    "assets/images/codex_pet_msg_key_1.c"
    "assets/images/codex_pet_msg_key_2.c"
    "assets/images/codex_pet_msg_error_0.c"
    "assets/images/codex_pet_msg_error_1.c"
    "assets/images/codex_pet_msg_error_2.c"
    "hal/hal_bluetooth.cpp"
)
list(REMOVE_DUPLICATES MY_SRCS)

set(MY_INCS
    "."
)

idf_component_register(
    SRCS 
        "main.cpp"
        ${MY_SRCS}
    INCLUDE_DIRS 
        ${MY_INCS}
    REQUIRES
        78__esp-wifi-connect
        ArduinoJson
        BMI270_BMM150_Sensor
        M5GFX
        M5IOE1
        M5PM1
        bt
        esp_hid
        esp_pm
        esp-dsp
        esp_http_client
        esp_codec_dev
        fatfs
        i2c_bus
        lvgl
        mbedtls
        mooncake
        mooncake_log
        smooth_ui_toolkit
    EMBED_TXTFILES
        "hal/utils/config_ap/assets/badge_config_ap.html"
    EMBED_FILES
        "assets/sfx/boot_sfx.bin"
)
