/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <memory>
#include <mooncake_log.h>
#include <nvs_flash.h>

static std::unique_ptr<Hal> _hal_instance;
static const std::string_view _tag = "HAL";

Hal& GetHAL()
{
    if (!_hal_instance) {
        mclog::tagInfo(_tag, "creating hal instance");
        _hal_instance = std::make_unique<Hal>();
    }
    return *_hal_instance.get();
}

void Hal::init()
{
    mclog::tagInfo(_tag, "init");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    configureCpuPower(false);

    i2c_init();
    pmic_init();
    ioe_init();
    delay(50);
    display_init();
    touchpad_init();
    lvgl_init();
    audio_init();
    imu_init();
    rtc_init();
    button_init();
    fs_init();
}

/* -------------------------------------------------------------------------- */
/*                                   System                                   */
/* -------------------------------------------------------------------------- */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_pm.h>
#include <driver/gpio.h>
#include <wifi_manager.h>
#include <cmath>

namespace {

constexpr std::uint32_t _activity_dim_timeout_ms = 1 * 60 * 1000;
constexpr std::uint32_t _activity_light_sleep_timeout_ms = 3 * 60 * 1000;
constexpr std::uint32_t _activity_deep_sleep_timeout_ms = 10 * 60 * 1000;
constexpr std::uint32_t _activity_poll_period_ms = 100;
constexpr int _activity_dim_brightness = 10;
constexpr float _activity_motion_threshold_g = 0.12f;
constexpr gpio_num_t _activity_wake_button_a = GPIO_NUM_2;
constexpr gpio_num_t _activity_wake_button_b = GPIO_NUM_1;

float accel_delta(const Hal::ImuData& a, const Hal::ImuData& b)
{
    const float dx = a.accelX - b.accelX;
    const float dy = a.accelY - b.accelY;
    const float dz = a.accelZ - b.accelZ;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void stop_wifi_radios()
{
    auto& wifi = WifiManager::GetInstance();
    wifi.StopConfigAp();
    wifi.StopStation();
}

}  // namespace

void Hal::delay(std::uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

std::uint32_t Hal::millis()
{
    return esp_timer_get_time() / 1000;
}

void Hal::feedTheDog()
{
    vTaskDelay(1);
}

std::array<uint8_t, 6> Hal::getFactoryMac()
{
    std::array<uint8_t, 6> mac;
    esp_efuse_mac_get_default(mac.data());
    return mac;
}

std::string Hal::getFactoryMacString(std::string divider)
{
    auto mac = getFactoryMac();
    return fmt::format("{:02X}{}{:02X}{}{:02X}{}{:02X}{}{:02X}{}{:02X}", mac[0], divider, mac[1], divider, mac[2],
                       divider, mac[3], divider, mac[4], divider, mac[5]);
}

void Hal::reboot()
{
    esp_restart();
}

void Hal::factoryReset()
{
    mclog::tagInfo(_tag, "start factory reset");
    ESP_ERROR_CHECK(nvs_flash_erase());
    reboot();
}

void Hal::markActivity()
{
    _activity_last_ms = millis();
    if (_activity_sleeping) {
        exitActivitySleep();
    } else if (_activity_dimmed) {
        exitActivityDim();
    }
}

bool Hal::pollUserActivity()
{
    bool active = false;

    const bool button_a_down = !gpio_get_level((gpio_num_t)2);
    const bool button_b_down = !gpio_get_level((gpio_num_t)1);
    if (button_a_down != _activity_button_a_down || button_b_down != _activity_button_b_down) {
        active = true;
        _activity_button_a_down = button_a_down;
        _activity_button_b_down = button_b_down;
    }

    if (_activity_sleeping) {
        auto touch = getTouchPoint();
        if (touch.num > 0) {
            active = true;
        }
    }

    updateImuData();
    const auto& imu = getImuData();
    if (!_activity_has_imu_baseline) {
        _activity_imu_baseline = imu;
        _activity_has_imu_baseline = true;
    } else if (accel_delta(imu, _activity_imu_baseline) > _activity_motion_threshold_g) {
        active = true;
        _activity_imu_baseline = imu;
    } else {
        _activity_imu_baseline.accelX = _activity_imu_baseline.accelX * 0.92f + imu.accelX * 0.08f;
        _activity_imu_baseline.accelY = _activity_imu_baseline.accelY * 0.92f + imu.accelY * 0.08f;
        _activity_imu_baseline.accelZ = _activity_imu_baseline.accelZ * 0.92f + imu.accelZ * 0.08f;
    }

    return active;
}

void Hal::updateActivityMonitor()
{
    const auto now = millis();
    if (_activity_last_ms == 0) {
        _activity_last_ms = now;
    }
    if (now - _activity_last_poll_ms < _activity_poll_period_ms) {
        return;
    }
    _activity_last_poll_ms = now;

    if (pollUserActivity()) {
        markActivity();
        return;
    }

    if (now < _activity_last_ms) {
        mclog::tagWarn(_tag, "activity timer moved backwards: now={} last={}", now, _activity_last_ms);
        _activity_last_ms = now;
        return;
    }

    const auto idle_ms = now - _activity_last_ms;
    if (idle_ms >= _activity_deep_sleep_timeout_ms) {
        if (isBatteryCharging(false)) {
            return;
        }
        enterDeepSleep();
        return;
    }

    if (!_activity_sleeping && idle_ms >= _activity_light_sleep_timeout_ms) {
        enterActivitySleep();
        return;
    }

    if (!_activity_sleeping && !_activity_dimmed && idle_ms >= _activity_dim_timeout_ms) {
        enterActivityDim();
    }
}

void Hal::enterActivityDim()
{
    if (_activity_dimmed || _activity_sleeping) {
        return;
    }

    _activity_dimmed = true;
    _activity_saved_brightness = getBackLightBrightness(false);
    mclog::tagInfo(_tag, "activity dim after {} ms idle", _activity_dim_timeout_ms);
    setBackLightBrightness(_activity_dim_brightness, false);
    configureCpuPower(true);
}

void Hal::exitActivityDim()
{
    if (!_activity_dimmed) {
        return;
    }

    _activity_dimmed = false;
    mclog::tagInfo(_tag, "activity dim wake");
    configureCpuPower(false);
    setBackLightBrightness(_activity_saved_brightness, false);
}

void Hal::enterActivitySleep()
{
    if (_activity_sleeping) {
        return;
    }

    _activity_sleeping = true;
    if (!_activity_dimmed) {
        _activity_saved_brightness = getBackLightBrightness(false);
    }
    mclog::tagInfo(_tag, "activity sleep after {} ms idle", _activity_light_sleep_timeout_ms);
    stop_wifi_radios();
    stopVibrate();
    stopLvglUpdate();
    setBackLightBrightness(0, false);
    delay(20);
    displayEnterActivitySleep();
}

void Hal::exitActivitySleep()
{
    if (!_activity_sleeping) {
        return;
    }

    _activity_sleeping = false;
    _activity_dimmed = false;
    mclog::tagInfo(_tag, "activity wake");
    configureCpuPower(false);
    displayExitActivitySleep();
    startLvglUpdate();
    requestFullDisplayRedraw();
    delay(30);
    setBackLightBrightness(_activity_saved_brightness, false);
    _activity_last_ms = millis();
}

void Hal::enterDeepSleep()
{
    constexpr uint64_t wake_mask = (1ULL << static_cast<uint8_t>(_activity_wake_button_a)) |
                                   (1ULL << static_cast<uint8_t>(_activity_wake_button_b));

    mclog::tagInfo(_tag, "deep sleep after {} ms idle", _activity_deep_sleep_timeout_ms);
    stop_wifi_radios();
    stopVibrate();
    stopLvglUpdate();
    setBackLightBrightness(0, false);

    gpio_set_direction(_activity_wake_button_a, GPIO_MODE_INPUT);
    gpio_set_pull_mode(_activity_wake_button_a, GPIO_PULLUP_ONLY);
    gpio_set_direction(_activity_wake_button_b, GPIO_MODE_INPUT);
    gpio_set_pull_mode(_activity_wake_button_b, GPIO_PULLUP_ONLY);

    ESP_ERROR_CHECK(esp_sleep_disable_ext1_wakeup_io(0));
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW));
    delay(50);
    esp_deep_sleep_start();
}

void Hal::configureCpuPower(bool lowPower)
{
#if CONFIG_PM_ENABLE
    if (_cpu_low_power_mode == lowPower) {
        return;
    }

    const esp_pm_config_t config = {
        .max_freq_mhz = lowPower ? 80 : 240,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    const esp_err_t ret = esp_pm_configure(&config);
    if (ret == ESP_OK) {
        _cpu_low_power_mode = lowPower;
        mclog::tagInfo(_tag, "cpu power mode: max={}MHz min={}MHz", config.max_freq_mhz, config.min_freq_mhz);
    } else {
        mclog::tagWarn(_tag, "cpu power mode configure failed: {}", esp_err_to_name(ret));
    }
#else
    (void)lowPower;
#endif
}

/* -------------------------------------------------------------------------- */
/*                                     I2C                                    */
/* -------------------------------------------------------------------------- */
#include <i2c_bus.h>

#define I2C_SCL_PIN (gpio_num_t)48
#define I2C_SDA_PIN (gpio_num_t)47

void Hal::i2c_init()
{
    mclog::tagInfo(_tag, "i2c init");

    i2c_config_t conf = {
        .mode          = I2C_MODE_MASTER,
        .sda_io_num    = I2C_SDA_PIN,
        .scl_io_num    = I2C_SCL_PIN,
        .sda_pullup_en = true,
        .scl_pullup_en = true,
        .master =
            {
                .clk_speed = 100000,
            },
        .clk_flags = 0,
    };
    _i2c_bus = i2c_bus_create(I2C_NUM_0, &conf);

    i2c_detect();
}

void Hal::i2c_detect()
{
    uint8_t address;
    uint8_t buf[128];
    uint8_t count = i2c_bus_scan(_i2c_bus, buf, 128);

    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address    = i + j;
            bool found = false;
            for (int k = 0; k < count; k++) {
                if (buf[k] == address) {
                    found = true;
                    break;
                }
            }
            if (found) {
                printf("%02x ", address);
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
}

/* -------------------------------------------------------------------------- */
/*                                    Guide                                   */
/* -------------------------------------------------------------------------- */
#include "utils/settings/settings.h"

namespace {

constexpr const char* _guide_launch_count_key = "launch_count";
constexpr int _guide_show_limit               = 5;

}  // namespace

bool Hal::shouldShowGuide()
{
    Settings settings(std::string(Hal::SettingsNs), true);
    const int launch_count = settings.GetInt(_guide_launch_count_key, 0);
    if (launch_count >= _guide_show_limit) {
        mclog::tagInfo(_tag, "skip guide, launch count: {}", launch_count);
        return false;
    }

    settings.SetInt(_guide_launch_count_key, launch_count + 1);
    mclog::tagInfo(_tag, "show guide, launch count: {} -> {}", launch_count, launch_count + 1);
    return true;
}
