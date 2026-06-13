/*
 * BLE HID bridge for Typeless/Codex control.
 */
#include "ble_bridge.h"

#include <apps/app_codex/codex_config.h>
#include "hal.h"
#include "utils/settings/settings.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <host/ble_att.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_sm.h>
#include <host/ble_store.h>
#include <host/ble_uuid.h>
#include <mooncake_log.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <os/os_mbuf.h>
#include <services/bas/ble_svc_bas.h>
#include <services/dis/ble_svc_dis.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
#include <services/hid/ble_svc_hid.h>

#include <ctime>
#include <sys/time.h>

namespace {

extern "C" void ble_store_config_init(void);

constexpr const char* kTag = "BLE-Bridge";
constexpr uint16_t kHidServiceUuid = BLE_SVC_HID_UUID16;
constexpr uint16_t kAppearanceKeyboard = 0x03C1;
constexpr uint16_t kVendorId = 0x02E5;
constexpr uint16_t kProductId = 0x05E2;
constexpr uint8_t kKeyboardReportId = 1;
constexpr uint8_t kConsumerReportId = 2;
constexpr uint8_t kKeyEnter = 0x28;
constexpr uint8_t kKeyBackspace = 0x2A;
constexpr uint8_t kKeyA = 0x04;
constexpr uint8_t kKeyEscape = 0x29;
constexpr uint8_t kKeyF19 = 0x6E;
constexpr uint8_t kModifierLeftGui = 0x08;
constexpr uint8_t kModifierRightAlt = 0x40;

enum class HostInputMode : uint8_t {
    Typeless,
    WechatIme,
};

struct HostKeyBinding {
    uint8_t modifier = 0;
    uint8_t keycode = 0;
};

const ble_uuid128_t kBridgeServiceUuid =
    BLE_UUID128_INIT(0x45, 0x4C, 0x42, 0x31, 0x2F, 0x2A, 0x44, 0x63, 0x94, 0xB3, 0x19, 0xE8, 0x00, 0x00, 0xCD, 0xAB);
const ble_uuid128_t kBridgeEventUuid =
    BLE_UUID128_INIT(0x45, 0x4C, 0x42, 0x31, 0x2F, 0x2A, 0x44, 0x63, 0x94, 0xB3, 0x19, 0xE8, 0x01, 0x00, 0xCD, 0xAB);
const ble_uuid128_t kBridgeStatusUuid =
    BLE_UUID128_INIT(0x45, 0x4C, 0x42, 0x31, 0x2F, 0x2A, 0x44, 0x63, 0x94, 0xB3, 0x19, 0xE8, 0x02, 0x00, 0xCD, 0xAB);
const ble_uuid128_t kBridgePanelUuid =
    BLE_UUID128_INIT(0x45, 0x4C, 0x42, 0x31, 0x2F, 0x2A, 0x44, 0x63, 0x94, 0xB3, 0x19, 0xE8, 0x03, 0x00, 0xCD, 0xAB);

const char kPnpId[7] = {
    0x02,
    static_cast<char>(kVendorId & 0xFF),
    static_cast<char>((kVendorId >> 8) & 0xFF),
    static_cast<char>(kProductId & 0xFF),
    static_cast<char>((kProductId >> 8) & 0xFF),
    0x01,
    0x01,
};

const uint8_t kKeyboardReportMap[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, kKeyboardReportId,
    0x05, 0x07,  // Usage Page (Keyboard/Keypad)
    0x19, 0xE0,  // Usage Minimum (Left Control)
    0x29, 0xE7,  // Usage Maximum (Right GUI)
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,  // Input (8 modifier bits)
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,  // Input (reserved, constant)
    0x95, 0x05,
    0x75, 0x01,
    0x05, 0x08,
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,  // Output (LEDs)
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x01,  // Output (LED padding)
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x26, 0xE7, 0x00,
    0x05, 0x07,
    0x19, 0x00,
    0x2A, 0xE7, 0x00,
    0x81, 0x00,  // Input (6 key slots)
    0xC0,

    0x05, 0x0C,  // Usage Page (Consumer)
    0x09, 0x01,  // Usage (Consumer Control)
    0xA1, 0x01,  // Collection (Application)
    0x85, kConsumerReportId,
    0x15, 0x00,
    0x26, 0xFF, 0x03,
    0x19, 0x00,
    0x2A, 0xFF, 0x03,
    0x75, 0x10,
    0x95, 0x01,
    0x81, 0x00,
    0xC0,
};

bool g_enabled = false;
bool g_started = false;
bool g_connected = false;
bool g_paired = false;
bool g_advertising = false;
bool g_random_addr_set = false;
char g_device_name[16] = "M5Codex-AA";
uint8_t g_own_addr_type = BLE_OWN_ADDR_RANDOM;
uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
uint16_t g_protocol_handle = 0;
uint16_t g_boot_keyboard_input_handle = 0;
uint16_t g_report_keyboard_input_handle = 0;
uint16_t g_bridge_event_handle = 0;
bool g_bridge_event_subscribed = false;
bool g_companion_limited = false;
const char* g_status_text = "BLE ready";
std::string g_host_status_text = "BLE ready";
uint32_t g_host_status_sequence = 1;
bool g_host_voice_valid = false;
bool g_host_voice_active = false;
ble_bridge::VoicePhase g_host_voice_phase = ble_bridge::VoicePhase::Idle;
uint32_t g_host_voice_sequence = 0;
TickType_t g_companion_ready_tick = 0;
bool g_host_panel_valid = false;
std::string g_host_panel_json;
uint32_t g_host_panel_sequence = 0;
HostInputMode g_host_input_mode = HostInputMode::Typeless;
HostKeyBinding g_primary_binding = {0, kKeyF19};
HostKeyBinding g_confirm_binding = {0, kKeyEnter};
HostKeyBinding g_shake_binding = {0, 0};
std::string g_shake_action = "Clear Input";
bool g_host_config_loaded = false;
int g_panel_seq = -1;
int g_panel_expected_parts = 0;
int g_panel_next_part = 1;
std::string g_panel_buffer;
TaskHandle_t g_battery_task_handle = nullptr;
uint8_t g_last_report[8] = {};

void advertise();
bool send_keyboard_report(uint8_t modifier, uint8_t keycode);
void release_keyboard();

void set_host_status(const char* status)
{
    g_host_status_text = status;
    ++g_host_status_sequence;
}

void update_ble_battery_level()
{
    const uint8_t level = std::min<uint8_t>(100, GetHAL().getBatteryLevel());
    ble_svc_bas_battery_level_set(level);
}

void battery_task(void*)
{
    while (true) {
        update_ble_battery_level();
        vTaskDelay(pdMS_TO_TICKS(codex_config::kBleBatteryUpdateMs));
    }
}

void mark_companion_ready()
{
    g_companion_ready_tick = xTaskGetTickCount();
    g_companion_limited = false;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains_any(const std::string& haystack, std::initializer_list<const char*> needles)
{
    for (const char* needle : needles) {
        if (haystack.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

uint8_t clamp_hid_byte(int value, uint8_t fallback)
{
    return value >= 0 && value <= 255 ? static_cast<uint8_t>(value) : fallback;
}

const char* input_mode_storage_value()
{
    return g_host_input_mode == HostInputMode::WechatIme ? "wechat_ime" : "typeless";
}

HostInputMode input_mode_from_string(const std::string& value)
{
    const std::string lower = lowercase(value);
    return contains_any(lower, {"wechat_ime", "wechat"}) ? HostInputMode::WechatIme : HostInputMode::Typeless;
}

void apply_default_bindings_for_mode(HostInputMode mode)
{
    g_host_input_mode = mode;
    if (mode == HostInputMode::WechatIme) {
        g_primary_binding = {kModifierRightAlt, 0};
        g_confirm_binding = {0, kKeyEnter};
    } else {
        g_primary_binding = {0, kKeyF19};
        g_confirm_binding = {0, kKeyEnter};
    }
}

void load_host_input_config()
{
    if (g_host_config_loaded) {
        return;
    }
    g_host_config_loaded = true;

    Settings settings("ble_bridge");
    g_host_input_mode = input_mode_from_string(settings.GetString("mode", "typeless"));
    g_primary_binding.modifier = clamp_hid_byte(settings.GetInt("primary_mod", g_primary_binding.modifier), g_primary_binding.modifier);
    g_primary_binding.keycode = clamp_hid_byte(settings.GetInt("primary_key", g_primary_binding.keycode), g_primary_binding.keycode);
    g_confirm_binding.modifier = clamp_hid_byte(settings.GetInt("confirm_mod", g_confirm_binding.modifier), g_confirm_binding.modifier);
    g_confirm_binding.keycode = clamp_hid_byte(settings.GetInt("confirm_key", g_confirm_binding.keycode), g_confirm_binding.keycode);
    g_shake_binding.modifier = clamp_hid_byte(settings.GetInt("shake_mod", g_shake_binding.modifier), g_shake_binding.modifier);
    g_shake_binding.keycode = clamp_hid_byte(settings.GetInt("shake_key", g_shake_binding.keycode), g_shake_binding.keycode);
    g_shake_action = settings.GetString("shake_action", g_shake_action);
    mclog::tagInfo(kTag,
                   "loaded input config: mode={} primary_mod=0x{:02x} primary_key=0x{:02x} confirm_mod=0x{:02x} confirm_key=0x{:02x} shake={} shake_mod=0x{:02x} shake_key=0x{:02x}",
                   input_mode_storage_value(),
                   static_cast<int>(g_primary_binding.modifier),
                   static_cast<int>(g_primary_binding.keycode),
                   static_cast<int>(g_confirm_binding.modifier),
                   static_cast<int>(g_confirm_binding.keycode),
                   g_shake_action,
                   static_cast<int>(g_shake_binding.modifier),
                   static_cast<int>(g_shake_binding.keycode));
}

void save_host_input_config()
{
    Settings settings("ble_bridge", true);
    settings.SetString("mode", input_mode_storage_value());
    settings.SetInt("primary_mod", g_primary_binding.modifier);
    settings.SetInt("primary_key", g_primary_binding.keycode);
    settings.SetInt("confirm_mod", g_confirm_binding.modifier);
    settings.SetInt("confirm_key", g_confirm_binding.keycode);
    settings.SetInt("shake_mod", g_shake_binding.modifier);
    settings.SetInt("shake_key", g_shake_binding.keycode);
    settings.SetString("shake_action", g_shake_action);
    mclog::tagInfo(kTag,
                   "saved input config: mode={} primary_mod=0x{:02x} primary_key=0x{:02x} confirm_mod=0x{:02x} confirm_key=0x{:02x} shake={} shake_mod=0x{:02x} shake_key=0x{:02x}",
                   input_mode_storage_value(),
                   static_cast<int>(g_primary_binding.modifier),
                   static_cast<int>(g_primary_binding.keycode),
                   static_cast<int>(g_confirm_binding.modifier),
                   static_cast<int>(g_confirm_binding.keycode),
                   g_shake_action,
                   static_cast<int>(g_shake_binding.modifier),
                   static_cast<int>(g_shake_binding.keycode));
}

bool send_hid_binding_down(const HostKeyBinding& binding)
{
    return send_keyboard_report(binding.modifier, binding.keycode);
}

bool send_hid_binding_tap(const HostKeyBinding& binding)
{
    if (send_hid_binding_down(binding)) {
        vTaskDelay(pdMS_TO_TICKS(codex_config::kBleKeyTapMs));
        release_keyboard();
        return true;
    }
    return false;
}

void execute_shake_hid_action()
{
    load_host_input_config();
    const std::string action = lowercase(g_shake_action);
    if (action == "none") {
        set_host_status("Shake disabled");
        return;
    }
    if (action == "escape") {
        set_host_status("Shake Escape");
        send_hid_binding_tap({0, kKeyEscape});
        return;
    }
    if (action == "return") {
        set_host_status("Shake Return");
        send_hid_binding_tap({0, kKeyEnter});
        return;
    }
    if (action.rfind("key:", 0) == 0 && g_shake_binding.keycode != 0) {
        set_host_status("Shake key");
        send_hid_binding_tap(g_shake_binding);
        return;
    }

    set_host_status("Shake Clear Input");
    if (send_hid_binding_tap({kModifierLeftGui, kKeyA})) {
        vTaskDelay(pdMS_TO_TICKS(codex_config::kBleKeyTapMs));
        send_hid_binding_tap({0, kKeyBackspace});
    }
}

int json_int_field(const std::string& json, const char* key, int fallback = -1)
{
    const std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    char* end = nullptr;
    const long value = std::strtol(json.c_str() + pos, &end, 10);
    return end == json.c_str() + pos ? fallback : static_cast<int>(value);
}

int64_t json_int64_field(const std::string& json, const char* key, int64_t fallback = 0)
{
    const std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) {
        return fallback;
    }
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos < json.size() && json[pos] == '"') {
        ++pos;
    }
    char* end = nullptr;
    const int64_t value = std::strtoll(json.c_str() + pos, &end, 10);
    if (end == json.c_str() + pos) {
        return fallback;
    }
    if (*end == '"' || *end == ',' || *end == '}' || std::isspace(static_cast<unsigned char>(*end))) {
        return value;
    }
    return fallback;
}

std::string json_string_field(const std::string& json, const char* key)
{
    const std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return {};
    }
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) {
        return {};
    }
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) {
        return {};
    }
    std::string out;
    bool escaped = false;
    for (++pos; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (escaped) {
            out.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return out;
        }
        out.push_back(ch);
    }
    return {};
}

int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + ch - 'A';
    }
    return -1;
}

bool append_hex(std::string& out, const std::string& hex)
{
    if (hex.size() % 2 != 0) {
        return false;
    }
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hex_value(hex[i]);
        const int lo = hex_value(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

bool system_time_looks_valid()
{
    const std::time_t now = std::time(nullptr);
    std::tm* timeinfo = std::localtime(&now);
    return timeinfo != nullptr && (timeinfo->tm_year + 1900) >= 2026;
}

void apply_panel_time(const std::string& panel_json)
{
    const int64_t epoch = json_int64_field(panel_json, "server_epoch", 0);
    if (epoch < 1600000000) {
        return;
    }

    const std::time_t now = std::time(nullptr);
    if (system_time_looks_valid() && std::llabs(static_cast<long long>(now - epoch)) <= 2) {
        ESP_LOGI(kTag, "panel time already current epoch=%lld", static_cast<long long>(epoch));
        return;
    }

    const struct timeval tv = {
        .tv_sec = static_cast<time_t>(epoch),
        .tv_usec = 0,
    };
    if (settimeofday(&tv, nullptr) != 0) {
        ESP_LOGW(kTag, "panel time apply failed");
        return;
    }
    GetHAL().syncSystemTimeToRtc();
    ESP_LOGI(kTag, "panel time synced epoch=%lld", static_cast<long long>(epoch));
}

void reset_panel_assembly()
{
    g_panel_seq = -1;
    g_panel_expected_parts = 0;
    g_panel_next_part = 1;
    g_panel_buffer.clear();
}

void apply_panel_payload(const std::string& payload)
{
    const int seq = json_int_field(payload, "seq");
    const int part = json_int_field(payload, "part");
    const int parts = json_int_field(payload, "parts");
    const std::string type = json_string_field(payload, "type");
    const std::string data_hex = json_string_field(payload, "data");

    if (type != "panel_chunk" || seq < 0 || part <= 0 || parts <= 0 || data_hex.empty()) {
        set_host_status("Panel chunk invalid");
        return;
    }
    if (part == 1) {
        reset_panel_assembly();
        g_panel_seq = seq;
        g_panel_expected_parts = parts;
        g_panel_next_part = 1;
    }
    if (seq != g_panel_seq || part != g_panel_next_part || parts != g_panel_expected_parts) {
        reset_panel_assembly();
        set_host_status("Panel chunk order failed");
        return;
    }
    if (!append_hex(g_panel_buffer, data_hex)) {
        reset_panel_assembly();
        set_host_status("Panel chunk decode failed");
        return;
    }

    ++g_panel_next_part;
    if (part == parts) {
        g_host_panel_json = g_panel_buffer;
        g_host_panel_valid = !g_host_panel_json.empty();
        ++g_host_panel_sequence;
        apply_panel_time(g_host_panel_json);
        reset_panel_assembly();
        set_host_status("Panel BLE synced");
        ESP_LOGI(kTag, "panel received via BLE: seq=%d bytes=%u", seq, static_cast<unsigned>(g_host_panel_json.size()));
    }
}

void apply_voice_payload(const std::string& payload)
{
    const std::string lower = lowercase(payload);
    if (contains_any(lower, {"\"type\":\"bridge_unavailable\"", "bridge_unavailable"})) {
        g_companion_ready_tick = 0;
        g_companion_limited = true;
        set_host_status("Bridge unavailable");
        return;
    }
    if (contains_any(lower, {"\"type\":\"bridge_limited\"", "bridge_limited", "\"helper\":\"limited\"", "helper_unavailable"})) {
        g_companion_ready_tick = 0;
        g_companion_limited = true;
        set_host_status("Bridge ready");
        return;
    }

    mark_companion_ready();
    if (contains_any(lower, {"\"input_mode\":", "\"primary_key\":", "\"confirm_key\":", "\"shake_action\":"})) {
        ESP_LOGI(kTag, "bridge config payload: %s", payload.c_str());
        release_keyboard();
        if (contains_any(lower, {"\"input_mode\":\"wechat_ime\"", "\"input_mode\":\"wechat\"", "\"mode\":\"wechat_ime\""})) {
            apply_default_bindings_for_mode(HostInputMode::WechatIme);
            set_host_status("WeChat IME mode");
        } else if (contains_any(lower, {"\"input_mode\":\"typeless\"", "\"mode\":\"typeless\""})) {
            apply_default_bindings_for_mode(HostInputMode::Typeless);
            set_host_status("Typeless mode");
        }

        g_primary_binding.modifier = clamp_hid_byte(json_int_field(payload, "primary_modifier", g_primary_binding.modifier), g_primary_binding.modifier);
        g_primary_binding.keycode = clamp_hid_byte(json_int_field(payload, "primary_key", g_primary_binding.keycode), g_primary_binding.keycode);
        g_confirm_binding.modifier = clamp_hid_byte(json_int_field(payload, "confirm_modifier", g_confirm_binding.modifier), g_confirm_binding.modifier);
        g_confirm_binding.keycode = clamp_hid_byte(json_int_field(payload, "confirm_key", g_confirm_binding.keycode), g_confirm_binding.keycode);
        g_shake_binding.modifier = clamp_hid_byte(json_int_field(payload, "shake_modifier", g_shake_binding.modifier), g_shake_binding.modifier);
        g_shake_binding.keycode = clamp_hid_byte(json_int_field(payload, "shake_key", g_shake_binding.keycode), g_shake_binding.keycode);
        const std::string shake_action = json_string_field(payload, "shake_action");
        if (!shake_action.empty()) {
            g_shake_action = shake_action;
        }
        ESP_LOGI(kTag,
                 "bridge config parsed: mode=%s primary_mod=0x%02x primary_key=0x%02x confirm_mod=0x%02x confirm_key=0x%02x shake=%s shake_mod=0x%02x shake_key=0x%02x",
                 g_host_input_mode == HostInputMode::WechatIme ? "wechat_ime" : "typeless",
                 static_cast<unsigned>(g_primary_binding.modifier),
                 static_cast<unsigned>(g_primary_binding.keycode),
                 static_cast<unsigned>(g_confirm_binding.modifier),
                 static_cast<unsigned>(g_confirm_binding.keycode),
                 g_shake_action.c_str(),
                 static_cast<unsigned>(g_shake_binding.modifier),
                 static_cast<unsigned>(g_shake_binding.keycode));
        g_host_config_loaded = true;
        save_host_input_config();
        return;
    }

    if (contains_any(lower, {"\"type\":\"bridge_ready\"", "bridge_ready"})) {
        set_host_status("Bridge ready");
        return;
    }

    bool active = g_host_voice_active;
    ble_bridge::VoicePhase phase = g_host_voice_phase;
    bool parsed = false;

    if (contains_any(lower, {"\"phase\":\"processing\"", "phase=processing", "processing", "处理中"})) {
        active = true;
        phase = ble_bridge::VoicePhase::Processing;
        parsed = true;
    }
    if (contains_any(lower, {"\"voice_active\":true", "\"active\":true", "voice_active=1", "active=1", "recording", "listening"})) {
        active = true;
        if (phase != ble_bridge::VoicePhase::Processing) {
            phase = ble_bridge::VoicePhase::Recording;
        }
        parsed = true;
    }
    if (contains_any(lower, {"\"voice_active\":false", "\"active\":false", "voice_active=0", "active=0", "idle", "stopped", "stop"})) {
        active = false;
        phase = ble_bridge::VoicePhase::Idle;
        parsed = true;
    }

    if (!parsed) {
        set_host_status("Host state unknown");
        return;
    }

    g_host_voice_valid = true;
    g_host_voice_active = active;
    g_host_voice_phase = phase;
    ++g_host_voice_sequence;
    if (phase == ble_bridge::VoicePhase::Processing) {
        set_host_status("Typeless processing");
    } else {
        set_host_status(active ? "Typeless recording" : "Typeless idle");
    }
    mclog::tagDebug(kTag, "host voice state updated: active={} phase={} payload={}",
                    active,
                    static_cast<int>(phase),
                    payload);
}

int bridge_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*)
{
    if (!ctxt) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const ble_uuid_t* uuid = ctxt->chr->uuid;
    if (ble_uuid_cmp(uuid, &kBridgePanelUuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            char buffer[320] = {};
            uint16_t out_len = 0;
            const int rc = ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer) - 1, &out_len);
            if (rc != 0) {
                return BLE_ATT_ERR_UNLIKELY;
            }
            buffer[out_len] = '\0';
            apply_panel_payload(std::string(buffer));
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ble_uuid_cmp(uuid, &kBridgeStatusUuid.u) == 0) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            char buffer[512] = {};
            uint16_t out_len = 0;
            const int rc = ble_hs_mbuf_to_flat(ctxt->om, buffer, sizeof(buffer) - 1, &out_len);
            if (rc != 0) {
                ESP_LOGW(kTag, "status/config payload too large or invalid: rc=%d max=%u", rc, static_cast<unsigned>(sizeof(buffer) - 1));
                return BLE_ATT_ERR_UNLIKELY;
            }
            buffer[out_len] = '\0';
            apply_voice_payload(std::string(buffer));
            return 0;
        }

        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            const char* state = g_host_voice_active ? "{\"voice_active\":true}" : "{\"voice_active\":false}";
            return os_mbuf_append(ctxt->om, state, strlen(state)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    }

    if (ble_uuid_cmp(uuid, &kBridgeEventUuid.u) == 0 && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char* state = "ready";
        return os_mbuf_append(ctxt->om, state, strlen(state)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

const ble_gatt_chr_def kBridgeCharacteristics[] = {
    {
        .uuid = &kBridgeEventUuid.u,
        .access_cb = bridge_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_bridge_event_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kBridgeStatusUuid.u,
        .access_cb = bridge_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &kBridgePanelUuid.u,
        .access_cb = bridge_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = nullptr,
        .access_cb = nullptr,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = 0,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
};

const ble_gatt_svc_def kBridgeServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kBridgeServiceUuid.u,
        .includes = nullptr,
        .characteristics = kBridgeCharacteristics,
    },
    {
        .type = 0,
        .uuid = nullptr,
        .includes = nullptr,
        .characteristics = nullptr,
    },
};

void host_task(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void register_callback(ble_gatt_register_ctxt* ctxt, void*)
{
    if (!ctxt) {
        return;
    }

    char uuid_text[BLE_UUID_STR_LEN] = {};
    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_CHR:
    {
        const uint16_t uuid16 = ble_uuid_u16(ctxt->chr.chr_def->uuid);
        ESP_LOGD(kTag,
                 "register chr %s val_handle=%u",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, uuid_text),
                 ctxt->chr.val_handle);
        if (uuid16 == BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE) {
            g_protocol_handle = ctxt->chr.val_handle;
        } else if (uuid16 == BLE_SVC_HID_CHR_UUID16_BOOT_KBD_INP) {
            g_boot_keyboard_input_handle = ctxt->chr.val_handle;
        } else if (ble_uuid_cmp(ctxt->chr.chr_def->uuid, &kBridgeEventUuid.u) == 0) {
            g_bridge_event_handle = ctxt->chr.val_handle;
        }
        break;
    }
    case BLE_GATT_REGISTER_OP_DSC:
    {
        const uint16_t uuid16 = ble_uuid_u16(ctxt->dsc.dsc_def->uuid);
        if (uuid16 != BLE_SVC_HID_DSC_UUID16_RPT_REF || ctxt->dsc.dsc_def->arg == nullptr) {
            break;
        }

        os_mbuf* om = nullptr;
        uint16_t report_info = 0;
        const int rc = ble_att_svr_read_local(ctxt->dsc.handle, &om);
        if (rc != 0 || om == nullptr) {
            ESP_LOGW(kTag, "read report reference failed: rc=%d", rc);
            break;
        }

        ble_hs_mbuf_to_flat(om, &report_info, sizeof(report_info), nullptr);
        os_mbuf_free_chain(om);

        const uint8_t report_id = report_info & 0x00FF;
        const uint8_t report_type = (report_info & 0xFF00) >> 8;
        const uint16_t report_handle = *static_cast<uint16_t*>(ctxt->dsc.dsc_def->arg);
        ESP_LOGD(kTag,
                 "register report ref id=%u type=%u handle=%u",
                 report_id,
                 report_type,
                 report_handle);

        if (report_id == kKeyboardReportId && report_type == BLE_SVC_HID_RPT_TYPE_INPUT) {
            g_report_keyboard_input_handle = report_handle;
        }
        break;
    }
    default:
        break;
    }
}

int gap_event(ble_gap_event* event, void*)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        g_advertising = false;
        if (event->connect.status == 0) {
            g_connected = true;
            g_conn_handle = event->connect.conn_handle;
            g_status_text = "BLE connected";
            update_ble_battery_level();
            set_host_status("BLE connected");
            ESP_LOGI(kTag, "connected: handle=%d", event->connect.conn_handle);
            const int rc = ble_gap_security_initiate(event->connect.conn_handle);
            ESP_LOGD(kTag, "security initiate: rc=%d", rc);
        } else {
            ESP_LOGW(kTag, "connect failed: status=%d", event->connect.status);
            set_host_status("BLE connect failed");
            if (g_enabled) {
                advertise();
            }
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        g_connected = false;
        g_paired = false;
        g_bridge_event_subscribed = false;
        g_companion_ready_tick = 0;
        g_companion_limited = false;
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_status_text = "BLE disconnected";
        set_host_status("BLE disconnected");
        ESP_LOGI(kTag, "disconnected: reason=%d", event->disconnect.reason);
        if (g_enabled) {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        g_advertising = false;
        ESP_LOGD(kTag, "advertise complete: reason=%d", event->adv_complete.reason);
        if (g_enabled && !g_connected) {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        g_paired = event->enc_change.status == 0;
        update_ble_battery_level();
        ESP_LOGI(kTag, "encryption changed: status=%d", event->enc_change.status);
        set_host_status(g_paired ? "BLE paired" : "Pairing failed");
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(kTag,
                 "subscribe: conn=%d attr=%d reason=%d notify %d->%d indicate %d->%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.reason,
                 event->subscribe.prev_notify,
                 event->subscribe.cur_notify,
                 event->subscribe.prev_indicate,
                 event->subscribe.cur_indicate);
        if (event->subscribe.attr_handle == g_bridge_event_handle) {
            g_bridge_event_subscribed = event->subscribe.cur_notify != 0;
            if (!g_bridge_event_subscribed) {
                g_companion_ready_tick = 0;
                g_companion_limited = false;
            }
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_TX:
        ESP_LOGD(kTag,
                 "notify tx: conn=%d attr=%d status=%d indication=%d",
                 event->notify_tx.conn_handle,
                 event->notify_tx.attr_handle,
                 event->notify_tx.status,
                 event->notify_tx.indication);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGD(kTag,
                 "mtu: conn=%d cid=%d mtu=%d",
                 event->mtu.conn_handle,
                 event->mtu.channel_id,
                 event->mtu.value);
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
    {
        ble_gap_conn_desc desc = {};
        const int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
            ESP_LOGI(kTag, "repeat pairing: deleted old bond");
            set_host_status("BLE re-pairing");
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        ESP_LOGW(kTag, "repeat pairing: find conn failed %d", rc);
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION:
    {
        ble_sm_io pkey = {};
        int rc = 0;
        ESP_LOGD(kTag, "passkey action: action=%d", event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = codex_config::kBlePairingPasskey;
            set_host_status("Pair code 000000");
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGD(kTag, "passkey inject: rc=%d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            pkey.action = event->passkey.params.action;
            pkey.numcmp_accept = 1;
            set_host_status("Pair confirm");
            ESP_LOGD(kTag,
                     "numeric compare accepted: %lu",
                     static_cast<unsigned long>(event->passkey.params.numcmp));
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGD(kTag, "numcmp inject: rc=%d", rc);
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            pkey.action = event->passkey.params.action;
            pkey.passkey = codex_config::kBlePairingPasskey;
            set_host_status("Pair input 000000");
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            ESP_LOGD(kTag, "input inject: rc=%d", rc);
        }
        return 0;
    }
    default:
        return 0;
    }
}

void sync_callback()
{
    g_started = true;
    ble_addr_t bonded_peers[4] = {};
    int bonded_peer_count = 0;
    const int bond_rc = ble_store_util_bonded_peers(bonded_peers, &bonded_peer_count, 4);
    ESP_LOGI(kTag,
             "host synced: protocol=%u boot_kbd=%u report_kbd=%u bonds=%d rc=%d",
             g_protocol_handle,
             g_boot_keyboard_input_handle,
             g_report_keyboard_input_handle,
             bonded_peer_count,
             bond_rc);
    advertise();
}

void reset_callback(int reason)
{
    g_started = false;
    g_connected = false;
    g_paired = false;
    g_advertising = false;
    g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ESP_LOGW(kTag, "host reset: reason=%d", reason);
    set_host_status("BLE host reset");
}

void prepare_identity()
{
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);

    const char suffix_a = static_cast<char>('A' + (mac[4] % 26));
    const char suffix_b = static_cast<char>('A' + (mac[5] % 26));
    std::snprintf(g_device_name, sizeof(g_device_name), "%s%c%c", codex_config::kBleDeviceNamePrefix, suffix_a, suffix_b);

    uint8_t random_addr[6] = {
        static_cast<uint8_t>((mac[5] ^ 0x1D) | 0xC0),
        static_cast<uint8_t>(mac[4] ^ 0x52),
        static_cast<uint8_t>(mac[3] ^ 0xA7),
        static_cast<uint8_t>(mac[2] ^ 0x39),
        static_cast<uint8_t>(mac[1] ^ 0x6B),
        static_cast<uint8_t>((mac[0] ^ 0xC3) | 0xC0),
    };

    const int addr_rc = ble_hs_id_set_rnd(random_addr);
    if (addr_rc != 0) {
        mclog::tagWarn(kTag, "set random addr failed: {}", addr_rc);
        set_host_status("BLE address failed");
        return;
    }

    g_random_addr_set = true;
    mclog::tagDebug(kTag, "identity prepared: {}", g_device_name);
}

void advertise()
{
    if (!g_enabled || !g_started || g_connected || g_advertising) {
        return;
    }

    if (!g_random_addr_set) {
        prepare_identity();
        if (!g_random_addr_set) {
            return;
        }
    }

    ble_svc_gap_device_name_set(g_device_name);
    ble_svc_gap_device_appearance_set(kAppearanceKeyboard);

    ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = kAppearanceKeyboard;
    fields.appearance_is_present = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = reinterpret_cast<const uint8_t*>(g_device_name);
    fields.name_len = strlen(g_device_name);
    fields.name_is_complete = 1;
    fields.uuids16 = (ble_uuid16_t[]) {BLE_UUID16_INIT(kHidServiceUuid), BLE_UUID16_INIT(BLE_SVC_BAS_UUID16)};
    fields.num_uuids16 = 2;
    fields.uuids16_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        mclog::tagWarn(kTag, "adv fields failed: {}", rc);
        set_host_status("BLE adv failed");
        return;
    }

    ble_hs_adv_fields rsp_fields = {};
    rsp_fields.uuids128 = &kBridgeServiceUuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        mclog::tagWarn(kTag, "scan response fields failed: {}", rc);
        set_host_status("BLE scan rsp failed");
        return;
    }

    ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);

    rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER, &adv_params, gap_event, nullptr);
    if (rc != 0) {
        mclog::tagWarn(kTag, "adv start failed: {}", rc);
        g_advertising = false;
        set_host_status("BLE adv start failed");
        return;
    }

    g_advertising = true;
    g_status_text = "BLE advertising";
    set_host_status("BLE ready");
    mclog::tagInfo(kTag, "HID advertising started");
}

bool notify_handle(uint16_t handle, const uint8_t* data, size_t len)
{
    if (handle == 0 || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }

    os_mbuf* om = ble_hs_mbuf_from_flat(data, len);
    if (om == nullptr) {
        ESP_LOGW(kTag, "notify mbuf alloc failed: handle=%u", handle);
        return false;
    }

    const int rc = ble_gatts_notify_custom(g_conn_handle, handle, om);
    if (rc != 0) {
        ESP_LOGW(kTag, "notify failed: handle=%u rc=%d", handle, rc);
        return false;
    }
    return true;
}

bool notify_bridge_event(const char* text)
{
    if (!text || g_bridge_event_handle == 0 || !g_connected) {
        return false;
    }
    return notify_handle(g_bridge_event_handle, reinterpret_cast<const uint8_t*>(text), strlen(text));
}

bool send_keyboard_report(uint8_t modifier, uint8_t keycode)
{
    if (!g_connected) {
        set_host_status("BLE not connected");
        return false;
    }

    uint8_t report[8] = {};
    report[0] = modifier;
    report[2] = keycode;
    memcpy(g_last_report, report, sizeof(g_last_report));

    const bool report_ok = notify_handle(g_report_keyboard_input_handle, report, sizeof(report));
    const bool boot_ok = notify_handle(g_boot_keyboard_input_handle, report, sizeof(report));
    if (!report_ok && !boot_ok) {
        set_host_status("Key send failed");
        return false;
    }

    mclog::tagInfo(kTag,
                  "key report sent: modifier=0x{:02x} key=0x{:02x} report_handle={} boot_handle={}",
                  static_cast<int>(modifier),
                  static_cast<int>(keycode),
                  g_report_keyboard_input_handle,
                  g_boot_keyboard_input_handle);
    set_host_status("Key sent");
    return true;
}

void release_keyboard()
{
    uint8_t report[8] = {};
    memcpy(g_last_report, report, sizeof(g_last_report));

    const bool report_ok = notify_handle(g_report_keyboard_input_handle, report, sizeof(report));
    const bool boot_ok = notify_handle(g_boot_keyboard_input_handle, report, sizeof(report));
    mclog::tagInfo(kTag,
                  "key report released: report_ok={} boot_ok={}",
                  report_ok,
                  boot_ok);
}

bool setup_hid_services()
{
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_bas_init();
    update_ble_battery_level();
    if (g_battery_task_handle == nullptr) {
        xTaskCreate(battery_task, "ble_battery", 3 * 1024, nullptr, 1, &g_battery_task_handle);
    }

    ble_svc_dis_init();
    ble_svc_dis_manufacturer_name_set("Liptox");
    ble_svc_dis_model_number_set("M5StopWatch-Codex");
    ble_svc_dis_serial_number_set("M5S3-Codex");
    ble_svc_dis_pnp_id_set(kPnpId);

    int rc = ble_gatts_count_cfg(kBridgeServices);
    if (rc != 0) {
        mclog::tagWarn(kTag, "bridge service count failed: {}", rc);
        set_host_status("Bridge add failed");
        return false;
    }
    rc = ble_gatts_add_svcs(kBridgeServices);
    if (rc != 0) {
        mclog::tagWarn(kTag, "bridge service add failed: {}", rc);
        set_host_status("Bridge add failed");
        return false;
    }

    ble_svc_hid_params hid = {};
    hid.proto_mode_present = 1;
    hid.proto_mode = BLE_SVC_HID_PROTO_MODE_REPORT;
    hid.kbd_inp_present = 1;
    hid.kbd_out_present = 1;
    hid.rpts_len = 3;
    hid.rpts[0].id = kKeyboardReportId;
    hid.rpts[0].type = BLE_SVC_HID_RPT_TYPE_INPUT;
    hid.rpts[0].len = sizeof(g_last_report);
    hid.rpts[1].id = kKeyboardReportId;
    hid.rpts[1].type = BLE_SVC_HID_RPT_TYPE_OUTPUT;
    hid.rpts[1].len = 1;
    hid.rpts[2].id = kConsumerReportId;
    hid.rpts[2].type = BLE_SVC_HID_RPT_TYPE_INPUT;
    hid.rpts[2].len = 2;
    memcpy(hid.report_map, kKeyboardReportMap, sizeof(kKeyboardReportMap));
    hid.report_map_len = sizeof(kKeyboardReportMap);
    hid.external_rpt_ref = BLE_SVC_BAS_UUID16;
    const uint8_t hid_info[4] = {
        0x11,
        0x01,
        0x00,
        0x01,
    };
    memcpy(&hid.hid_info, hid_info, sizeof(hid_info));

    rc = ble_svc_hid_add(hid);
    if (rc != 0) {
        mclog::tagWarn(kTag, "hid service add failed: {}", rc);
        set_host_status("HID add failed");
        return false;
    }

    ble_svc_hid_init();
    return true;
}

void start_ble()
{
    load_host_input_config();
    if (g_started) {
        advertise();
        return;
    }

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        mclog::tagWarn(kTag, "nimble init failed: {}", rc);
        set_host_status("BLE init failed");
        return;
    }

    ble_hs_cfg.reset_cb = reset_callback;
    ble_hs_cfg.sync_cb = sync_callback;
    ble_hs_cfg.gatts_register_cb = register_callback;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;

    if (!setup_hid_services()) {
        return;
    }

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    esp_err_t err = esp_nimble_enable(reinterpret_cast<void*>(host_task));
    if (err != ESP_OK) {
        mclog::tagWarn(kTag, "nimble host start failed: {}", static_cast<int>(err));
        set_host_status("BLE host failed");
        return;
    }

    g_status_text = "BLE HID starting";
    set_host_status("BLE starting");
    mclog::tagInfo(kTag, "HID keyboard starting");
}

}  // namespace

namespace ble_bridge {

void set_enabled(bool enabled)
{
    if (enabled) {
        g_enabled = true;
        start_ble();
    } else {
        g_enabled = false;
        g_status_text = "BLE disabled";
        set_host_status("BLE disabled");
        mclog::tagInfo(kTag, "disabled flag set; radio remains initialized until reboot");
    }
}

bool is_enabled()
{
    return g_enabled;
}

bool is_connected()
{
    return g_connected;
}

bool disconnect_current()
{
    if (!g_connected || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        set_host_status("BLE not connected");
        return false;
    }

    const uint16_t handle = g_conn_handle;
    set_host_status("BLE disconnecting");
    const int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        ESP_LOGW(kTag, "disconnect failed: handle=%u rc=%d", handle, rc);
        set_host_status("BLE disconnect failed");
        return false;
    }
    mclog::tagInfo(kTag, "disconnect requested: handle={}", handle);
    return true;
}

bool is_typeless_input_mode()
{
    load_host_input_config();
    return g_host_input_mode == HostInputMode::Typeless;
}

void send_typeless_option(ButtonAction action)
{
    load_host_input_config();
    if (action == ButtonAction::Down) {
        notify_bridge_event("input_primary_down");
        if (g_host_input_mode == HostInputMode::WechatIme) {
            mclog::tagInfo(kTag, "Primary input down via HID binding");
            set_host_status("WeChat input down");
        } else {
            mclog::tagInfo(kTag, "Primary input down via HID binding");
            set_host_status("Typeless input down");
        }
        send_hid_binding_down(g_primary_binding);
        return;
    }

    if (action == ButtonAction::Tap) {
        notify_bridge_event("input_primary_tap");
        if (g_host_input_mode == HostInputMode::WechatIme) {
            mclog::tagInfo(kTag, "Primary input tap via HID binding");
            set_host_status("WeChat input tap");
        } else {
            mclog::tagInfo(kTag, "Primary input tap via HID binding");
            set_host_status("Typeless input tap");
        }
        send_hid_binding_tap(g_primary_binding);
        return;
    }

    notify_bridge_event("input_primary_up");
    if (g_host_input_mode == HostInputMode::WechatIme) {
        mclog::tagInfo(kTag, "Primary input up via HID binding release");
        set_host_status("WeChat input up");
    } else {
        mclog::tagInfo(kTag, "Primary input up via HID binding release");
        set_host_status("Typeless input up");
    }
    release_keyboard();
}

void send_codex_enter()
{
    load_host_input_config();
    notify_bridge_event("input_confirm_tap");
    set_host_status("Input confirm key");
    send_hid_binding_tap(g_confirm_binding);
}

void send_shake_action()
{
    load_host_input_config();
    notify_bridge_event("shake_action");
    mclog::tagInfo(kTag, "Shake action via HID: {}", g_shake_action);
    execute_shake_hid_action();
}

std::string status_text()
{
    return g_status_text;
}

std::string host_status_text()
{
    return g_host_status_text;
}

uint32_t host_status_sequence()
{
    return g_host_status_sequence;
}

bool host_voice_valid()
{
    return g_host_voice_valid;
}

bool host_voice_active()
{
    return g_host_voice_active;
}

VoicePhase host_voice_phase()
{
    return g_host_voice_phase;
}

uint32_t host_voice_sequence()
{
    return g_host_voice_sequence;
}

bool host_panel_valid()
{
    return g_host_panel_valid;
}

std::string host_panel_json()
{
    return g_host_panel_json;
}

uint32_t host_panel_sequence()
{
    return g_host_panel_sequence;
}

}  // namespace ble_bridge
