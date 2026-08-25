#include "codex_micro_hid.h"

#include "hal.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <host/ble_att.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <os/os_mbuf.h>

namespace {

constexpr const char* kTag = "Codex-Micro-HID";
constexpr const char* kFirmwareVersion = "0.10.6-stopwatch";
constexpr uint8_t kKeyboardReportId = 1;
constexpr uint8_t kConsumerReportId = 2;
constexpr uint8_t kReportId = 6;
constexpr uint8_t kRpcChannel = 2;
constexpr size_t kReportBodySize = 63;
constexpr size_t kPayloadSize = 61;
constexpr size_t kMaxRpcBuffer = 4096;
constexpr size_t kThreadSlotCount = 6;
constexpr uint32_t kWorkingColor = 0x304FFE;
constexpr uint32_t kUnreadColor = 0x00FF4C;
constexpr uint32_t kAttentionColor = 0xFF6D00;
constexpr uint32_t kErrorColor = 0xFF0033;

const ble_uuid16_t kHidServiceUuid = BLE_UUID16_INIT(0x1812);
const ble_uuid16_t kProtocolModeUuid = BLE_UUID16_INIT(0x2A4E);
const ble_uuid16_t kReportMapUuid = BLE_UUID16_INIT(0x2A4B);
const ble_uuid16_t kReportUuid = BLE_UUID16_INIT(0x2A4D);
const ble_uuid16_t kHidInfoUuid = BLE_UUID16_INIT(0x2A4A);
const ble_uuid16_t kControlPointUuid = BLE_UUID16_INIT(0x2A4C);
const ble_uuid16_t kReportReferenceUuid = BLE_UUID16_INIT(0x2908);
const ble_uuid16_t kBootKeyboardInputUuid = BLE_UUID16_INIT(0x2A22);
const ble_uuid16_t kBootKeyboardOutputUuid = BLE_UUID16_INIT(0x2A32);

// A single HOGP service must describe every collection. macOS only exposed the
// first of the old, duplicated HID services, which left the vendor-defined
// Codex channel invisible to node-hid. Keep the proven keyboard and consumer
// reports, then append the Codex Micro vendor collection.
const uint8_t kUnifiedReportMap[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, kKeyboardReportId,
    0x05, 0x07,
    0x19, 0xE0,
    0x29, 0xE7,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,
    0x95, 0x05,
    0x75, 0x01,
    0x05, 0x08,
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x01,
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x26, 0xE7, 0x00,
    0x05, 0x07,
    0x19, 0x00,
    0x2A, 0xE7, 0x00,
    0x81, 0x00,
    0xC0,

    0x05, 0x0C,  // Usage Page (Consumer)
    0x09, 0x01,
    0xA1, 0x01,
    0x85, kConsumerReportId,
    0x15, 0x00,
    0x26, 0xFF, 0x03,
    0x19, 0x00,
    0x2A, 0xFF, 0x03,
    0x75, 0x10,
    0x95, 0x01,
    0x81, 0x00,
    0xC0,

    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (1)
    0xA1, 0x01,        // Collection (Application)
    0x85, kReportId,   // Report ID (6)
    0x15, 0x00,        // Logical Minimum (0)
    0x26, 0xFF, 0x00,  // Logical Maximum (255)
    0x75, 0x08,        // Report Size (8)
    0x95, 0x3F,        // Report Count (63)
    0x09, 0x01,        // Usage (1)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    0x95, 0x3F,        // Report Count (63)
    0x09, 0x02,        // Usage (2)
    0x91, 0x02,        // Output (Data, Variable, Absolute)
    0xC0,              // End Collection
};

const uint8_t kKeyboardInputReportReference[2] = {kKeyboardReportId, 0x01};
const uint8_t kKeyboardOutputReportReference[2] = {kKeyboardReportId, 0x02};
const uint8_t kConsumerInputReportReference[2] = {kConsumerReportId, 0x01};
const uint8_t kInputReportReference[2] = {kReportId, 0x01};
const uint8_t kOutputReportReference[2] = {kReportId, 0x02};
const uint8_t kHidInfo[4] = {0x11, 0x01, 0x00, 0x01};

enum class Attribute : uintptr_t {
    ProtocolMode = 1,
    ReportMap,
    KeyboardInputReport,
    KeyboardOutputReport,
    ConsumerInputReport,
    VendorInputReport,
    VendorOutputReport,
    HidInfo,
    ControlPoint,
    BootKeyboardInput,
    BootKeyboardOutput,
};

struct RxFrame {
    uint8_t length = 0;
    uint8_t data[kReportBodySize] = {};
};

struct ThreadSlot {
    bool assigned = false;
    uint32_t color = 0;
    float brightness = 0.0f;
    uint8_t effect = 0;
    float speed = 0.0f;
};

uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
uint16_t g_keyboard_input_handle = 0;
uint16_t g_keyboard_output_handle = 0;
uint16_t g_consumer_input_handle = 0;
uint16_t g_input_handle = 0;
uint16_t g_output_handle = 0;
uint16_t g_boot_keyboard_input_handle = 0;
uint16_t g_boot_keyboard_output_handle = 0;
bool g_input_subscribed = false;
uint8_t g_protocol_mode = 1;
uint8_t g_control_point = 0;
std::array<uint8_t, 8> g_keyboard_input_report = {};
uint8_t g_keyboard_output_report = 0;
std::array<uint8_t, 2> g_consumer_input_report = {};
std::array<uint8_t, kReportBodySize> g_input_report = {};
std::array<uint8_t, kReportBodySize> g_output_report = {};
std::array<uint8_t, 8> g_boot_keyboard_input_report = {};
uint8_t g_boot_keyboard_output_report = 0;
QueueHandle_t g_rx_queue = nullptr;
TaskHandle_t g_rpc_task_handle = nullptr;
SemaphoreHandle_t g_tx_mutex = nullptr;
SemaphoreHandle_t g_state_mutex = nullptr;
std::string g_rpc_buffer;
std::array<ThreadSlot, kThreadSlotCount> g_thread_slots = {};
uint32_t g_thread_status_sequence = 0;
bool g_host_rpc_seen = false;
uint32_t g_vendor_output_frames = 0;
uint32_t g_notify_attempts = 0;
uint32_t g_notify_successes = 0;
uint32_t g_notify_failures = 0;
bool g_warned_unsubscribed_notify = false;

Attribute attribute_from_arg(void* arg)
{
    return static_cast<Attribute>(reinterpret_cast<uintptr_t>(arg));
}

int append_value(os_mbuf* om, const void* data, size_t length)
{
    return os_mbuf_append(om, data, length) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

bool notify_body(const uint8_t* body, size_t length)
{
    if (!body || length != kReportBodySize ||
        g_input_handle == 0 || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }

    ++g_notify_attempts;
    if (!g_input_subscribed && !g_warned_unsubscribed_notify) {
        // The macOS HID stack can keep the ATT notification path active while
        // the last local SUBSCRIBE event is stale or missing after a cached BLE
        // reconnect.  ble_gatts_notify_custom() is the source of truth: it
        // validates the live connection and returns the actual delivery result.
        // Do not suppress an RPC reply solely because our cached flag is false.
        ESP_LOGW(kTag,
                 "vendor notify attempt without cached subscription: attempt=%lu",
                 static_cast<unsigned long>(g_notify_attempts));
        g_warned_unsubscribed_notify = true;
    }

    os_mbuf* om = ble_hs_mbuf_from_flat(body, length);
    if (!om) {
        ESP_LOGW(kTag, "input notify allocation failed");
        ++g_notify_failures;
        return false;
    }
    const int rc = ble_gatts_notify_custom(g_conn_handle, g_input_handle, om);
    if (rc != 0) {
        ++g_notify_failures;
        ESP_LOGW(kTag,
                 "input notify failed: rc=%d subscribed=%d attempts=%lu failures=%lu",
                 rc,
                 g_input_subscribed,
                 static_cast<unsigned long>(g_notify_attempts),
                 static_cast<unsigned long>(g_notify_failures));
        return false;
    }
    ++g_notify_successes;
    return true;
}

bool send_json(const std::string& json)
{
    if (!g_tx_mutex ||
        xSemaphoreTake(g_tx_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return false;
    }

    bool sent = true;
    std::string framed = json;
    framed.push_back('\n');
    size_t offset = 0;
    while (offset < framed.size()) {
        const size_t chunk = std::min(kPayloadSize, framed.size() - offset);
        g_input_report.fill(0);
        g_input_report[0] = kRpcChannel;
        g_input_report[1] = static_cast<uint8_t>(chunk);
        std::memcpy(g_input_report.data() + 2, framed.data() + offset, chunk);
        if (!notify_body(g_input_report.data(), g_input_report.size())) {
            sent = false;
            break;
        }
        offset += chunk;
        if (offset < framed.size()) {
            vTaskDelay(pdMS_TO_TICKS(4));
        }
    }
    xSemaphoreGive(g_tx_mutex);
    return sent;
}

template <typename TDoc>
bool send_response(JsonVariantConst id, const TDoc& response)
{
    if (id.isNull()) {
        return false;
    }
    JsonDocument envelope;
    envelope["id"] = id;
    envelope["result"] = response.template as<JsonVariantConst>();
    std::string json;
    serializeJson(envelope, json);
    const bool sent = send_json(json);
    ESP_LOGI(kTag,
             "RPC response: bytes=%u sent=%d subscribed=%d notify=%lu/%lu",
             static_cast<unsigned>(json.size()),
             sent,
             g_input_subscribed,
             static_cast<unsigned long>(g_notify_successes),
             static_cast<unsigned long>(g_notify_attempts));
    return sent;
}

bool send_success(JsonVariantConst id)
{
    JsonDocument result;
    result["ok"] = true;
    return send_response(id, result);
}

void update_thread_status(JsonArrayConst values)
{
    if (!g_state_mutex || xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return;
    }

    bool changed = false;
    for (JsonObjectConst value : values) {
        const int index = value["id"] | -1;
        if (index < 0 || index >= static_cast<int>(g_thread_slots.size())) {
            continue;
        }
        ThreadSlot& slot = g_thread_slots[static_cast<size_t>(index)];
        const uint32_t color = value["c"] | slot.color;
        const float brightness = value["b"] | slot.brightness;
        uint8_t effect = slot.effect;
        const JsonVariantConst effect_value = value["e"];
        if (effect_value.is<int>()) {
            effect = static_cast<uint8_t>(std::clamp(effect_value.as<int>(), 0, 6));
        } else if (effect_value.is<const char*>()) {
            const char* name = effect_value.as<const char*>();
            if (std::strcmp(name, "off") == 0) {
                effect = 0;
            } else if (std::strcmp(name, "solid") == 0) {
                effect = 1;
            } else if (std::strcmp(name, "snake") == 0) {
                effect = 2;
            } else if (std::strcmp(name, "rainbow") == 0) {
                effect = 3;
            } else if (std::strcmp(name, "breath") == 0) {
                effect = 4;
            } else if (std::strcmp(name, "gradient") == 0) {
                effect = 5;
            } else if (std::strcmp(name, "shallowBreath") == 0 ||
                       std::strcmp(name, "shallow_breath") == 0) {
                effect = 6;
            }
        } else if (brightness > 0.01f && effect == 0) {
            // Some host updates only refresh color/brightness. Treat them as
            // solid instead of hiding a newly assigned Agent slot.
            effect = 1;
        }
        const float speed = std::clamp(value["s"] | slot.speed, 0.0f, 1.0f);
        const bool assigned = brightness > 0.01f && effect != 0;
        if (slot.assigned != assigned || slot.color != color ||
            slot.brightness != brightness || slot.effect != effect || slot.speed != speed) {
            slot.assigned = assigned;
            slot.color = color;
            slot.brightness = brightness;
            slot.effect = effect;
            slot.speed = speed;
            changed = true;
        }
    }
    if (changed) {
        ++g_thread_status_sequence;
    }
    xSemaphoreGive(g_state_mutex);
}

void reset_thread_status()
{
    if (!g_state_mutex || xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return;
    }
    g_thread_slots.fill({});
    g_host_rpc_seen = false;
    ++g_thread_status_sequence;
    xSemaphoreGive(g_state_mutex);
}

void mark_host_communicating()
{
    if (!g_state_mutex || xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    if (!g_host_rpc_seen) {
        g_host_rpc_seen = true;
        ++g_thread_status_sequence;
    }
    xSemaphoreGive(g_state_mutex);
}

void handle_rpc(const JsonDocument& request)
{
    const char* method = request["method"] | "";
    if (method[0] == '\0') {
        method = request["m"] | "";
    }
    const JsonVariantConst id = request["id"];
    ESP_LOGI(kTag,
             "RPC request: method=%s id=%ld subscribed=%d frames=%lu",
             method,
             id.is<long>() ? id.as<long>() : -1L,
             g_input_subscribed,
             static_cast<unsigned long>(g_vendor_output_frames));

    if (std::strcmp(method, "sys.version") == 0) {
        JsonDocument result;
        result["version"] = kFirmwareVersion;
        if (send_response(id, result)) {
            mark_host_communicating();
        }
        return;
    }

    if (std::strcmp(method, "device.status") == 0) {
        JsonDocument result;
        result["version"] = kFirmwareVersion;
        result["profile_index"] = 0;
        result["layer_index"] = 1;
        result["battery"] = std::min<uint8_t>(100, GetHAL().getBatteryLevel());
        result["is_charging"] = GetHAL().isBatteryCharging();
        if (send_response(id, result)) {
            mark_host_communicating();
        }
        return;
    }

    if (std::strcmp(method, "v.oai.thstatus") == 0) {
        const JsonVariantConst params = request["params"];
        if (params.is<JsonArrayConst>()) {
            update_thread_status(params.as<JsonArrayConst>());
        }
        if (send_success(id)) {
            mark_host_communicating();
        }
        return;
    }

    if (std::strcmp(method, "v.oai.rgbcfg") == 0 ||
        std::strcmp(method, "lights.preview") == 0 ||
        std::strcmp(method, "host.focused_app") == 0) {
        if (send_success(id)) {
            mark_host_communicating();
        }
        return;
    }

    if (!id.isNull()) {
        JsonDocument envelope;
        envelope["id"] = id;
        JsonObject error = envelope["error"].to<JsonObject>();
        error["code"] = -32601;
        error["message"] = "Method not found";
        std::string json;
        serializeJson(envelope, json);
        if (send_json(json)) {
            mark_host_communicating();
        }
    }
}

void ingest_output_frame(const RxFrame& frame)
{
    if (frame.length < 2) {
        return;
    }

    size_t offset = 0;
    if (frame.length >= 3 && frame.data[0] == kReportId) {
        offset = 1;
    }
    if (frame.length < offset + 2 || frame.data[offset] != kRpcChannel) {
        return;
    }

    const size_t payload_length = std::min<size_t>(frame.data[offset + 1], kPayloadSize);
    if (frame.length < offset + 2 + payload_length) {
        ESP_LOGW(kTag, "short RPC fragment: len=%u payload=%u",
                 frame.length,
                 static_cast<unsigned>(payload_length));
        return;
    }

    const char* payload = reinterpret_cast<const char*>(frame.data + offset + 2);
    constexpr char kTopLevelPrefix[] = "{\"method\"";
    const bool starts_top_level =
        payload_length >= sizeof(kTopLevelPrefix) - 1 &&
        std::memcmp(payload, kTopLevelPrefix, sizeof(kTopLevelPrefix) - 1) == 0;
    if (starts_top_level && !g_rpc_buffer.empty()) {
        g_rpc_buffer.clear();
    }

    size_t json_start = 0;
    if (g_rpc_buffer.empty()) {
        while (json_start < payload_length && payload[json_start] != '{') {
            ++json_start;
        }
        if (json_start == payload_length) {
            return;
        }
    }

    const size_t append_length = payload_length - json_start;
    if (g_rpc_buffer.size() + append_length > kMaxRpcBuffer) {
        ESP_LOGW(kTag, "RPC buffer overflow; dropping request");
        g_rpc_buffer.clear();
        return;
    }
    g_rpc_buffer.append(payload + json_start, append_length);

    JsonDocument request;
    const DeserializationError error = deserializeJson(request, g_rpc_buffer);
    if (error == DeserializationError::IncompleteInput) {
        return;
    }
    if (error) {
        ESP_LOGW(kTag, "RPC parse failed: %s", error.c_str());
        g_rpc_buffer.clear();
        return;
    }

    handle_rpc(request);
    g_rpc_buffer.clear();
}

void rpc_task(void*)
{
    RxFrame frame;
    while (true) {
        if (xQueueReceive(g_rx_queue, &frame, portMAX_DELAY) == pdTRUE) {
            ingest_output_frame(frame);
        }
    }
}

int gatt_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void* arg)
{
    if (!ctxt) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const Attribute attribute = attribute_from_arg(arg);
    switch (attribute) {
    case Attribute::ProtocolMode:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om, &g_protocol_mode, sizeof(g_protocol_mode));
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return ble_hs_mbuf_to_flat(ctxt->om, &g_protocol_mode, sizeof(g_protocol_mode), nullptr) == 0
                ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        break;
    case Attribute::ReportMap:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om, kUnifiedReportMap, sizeof(kUnifiedReportMap));
        }
        break;
    case Attribute::KeyboardInputReport:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om,
                                g_keyboard_input_report.data(),
                                g_keyboard_input_report.size());
        }
        break;
    case Attribute::KeyboardOutputReport:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om,
                                &g_keyboard_output_report,
                                sizeof(g_keyboard_output_report));
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return ble_hs_mbuf_to_flat(ctxt->om,
                                       &g_keyboard_output_report,
                                       sizeof(g_keyboard_output_report),
                                       nullptr) == 0
                ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        break;
    case Attribute::ConsumerInputReport:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om,
                                g_consumer_input_report.data(),
                                g_consumer_input_report.size());
        }
        break;
    case Attribute::VendorInputReport:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om, g_input_report.data(), g_input_report.size());
        }
        break;
    case Attribute::VendorOutputReport:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om, g_output_report.data(), g_output_report.size());
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            RxFrame frame;
            uint16_t output_length = 0;
            const int rc = ble_hs_mbuf_to_flat(ctxt->om,
                                               frame.data,
                                               sizeof(frame.data),
                                               &output_length);
            if (rc != 0 || output_length > sizeof(frame.data)) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            frame.length = static_cast<uint8_t>(output_length);
            ++g_vendor_output_frames;
            ESP_LOGD(kTag,
                     "vendor output report: len=%u first=0x%02x frames=%lu",
                     static_cast<unsigned>(output_length),
                     output_length > 0 ? frame.data[0] : 0,
                     static_cast<unsigned long>(g_vendor_output_frames));
            g_output_report.fill(0);
            std::memcpy(g_output_report.data(), frame.data, output_length);
            if (!g_rx_queue || xQueueSend(g_rx_queue, &frame, 0) != pdTRUE) {
                ESP_LOGW(kTag, "RPC queue full; rejecting output report");
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return 0;
        }
        break;
    case Attribute::HidInfo:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om, kHidInfo, sizeof(kHidInfo));
        }
        break;
    case Attribute::ControlPoint:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return ble_hs_mbuf_to_flat(ctxt->om, &g_control_point, sizeof(g_control_point), nullptr) == 0
                ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        break;
    case Attribute::BootKeyboardInput:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om,
                                g_boot_keyboard_input_report.data(),
                                g_boot_keyboard_input_report.size());
        }
        break;
    case Attribute::BootKeyboardOutput:
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_value(ctxt->om,
                                &g_boot_keyboard_output_report,
                                sizeof(g_boot_keyboard_output_report));
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            return ble_hs_mbuf_to_flat(ctxt->om,
                                       &g_boot_keyboard_output_report,
                                       sizeof(g_boot_keyboard_output_report),
                                       nullptr) == 0
                ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        break;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

int report_reference_access(uint16_t,
                            uint16_t,
                            ble_gatt_access_ctxt* ctxt,
                            void* arg)
{
    if (!ctxt || ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const uint8_t* reference = nullptr;
    if (arg == &g_keyboard_input_handle) {
        reference = kKeyboardInputReportReference;
    } else if (arg == &g_keyboard_output_handle) {
        reference = kKeyboardOutputReportReference;
    } else if (arg == &g_consumer_input_handle) {
        reference = kConsumerInputReportReference;
    } else if (arg == &g_input_handle) {
        reference = kInputReportReference;
    } else if (arg == &g_output_handle) {
        reference = kOutputReportReference;
    }
    return reference
        ? append_value(ctxt->om, reference, 2)
        : BLE_ATT_ERR_UNLIKELY;
}

#define REPORT_DESCRIPTOR_ARRAY(name, handle) \
ble_gatt_dsc_def name[] = { \
    { \
        .uuid = &kReportReferenceUuid.u, \
        .att_flags = BLE_ATT_F_READ, \
        .min_key_size = 0, \
        .access_cb = report_reference_access, \
        .arg = &(handle), \
    }, \
    {}, \
}

REPORT_DESCRIPTOR_ARRAY(kKeyboardInputDescriptors, g_keyboard_input_handle);
REPORT_DESCRIPTOR_ARRAY(kKeyboardOutputDescriptors, g_keyboard_output_handle);
REPORT_DESCRIPTOR_ARRAY(kConsumerInputDescriptors, g_consumer_input_handle);
REPORT_DESCRIPTOR_ARRAY(kInputDescriptors, g_input_handle);
REPORT_DESCRIPTOR_ARRAY(kOutputDescriptors, g_output_handle);

#undef REPORT_DESCRIPTOR_ARRAY

const ble_gatt_chr_def kCharacteristics[] = {
    {
        .uuid = &kProtocolModeUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::ProtocolMode)),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &kReportMapUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::ReportMap)),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &kReportUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::KeyboardInputReport)),
        .descriptors = kKeyboardInputDescriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_keyboard_input_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kReportUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::KeyboardOutputReport)),
        .descriptors = kKeyboardOutputDescriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = &g_keyboard_output_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kReportUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::ConsumerInputReport)),
        .descriptors = kConsumerInputDescriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_consumer_input_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kReportUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::VendorInputReport)),
        .descriptors = kInputDescriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_input_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kReportUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::VendorOutputReport)),
        .descriptors = kOutputDescriptors,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = &g_output_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kHidInfoUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::HidInfo)),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &kControlPointUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::ControlPoint)),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &kBootKeyboardInputUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::BootKeyboardInput)),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_boot_keyboard_input_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kBootKeyboardOutputUuid.u,
        .access_cb = gatt_access,
        .arg = reinterpret_cast<void*>(static_cast<uintptr_t>(Attribute::BootKeyboardOutput)),
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = &g_boot_keyboard_output_handle,
        .cpfd = nullptr,
    },
    {},
};

const ble_gatt_svc_def kServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kHidServiceUuid.u,
        .includes = nullptr,
        .characteristics = kCharacteristics,
    },
    {},
};

}  // namespace

namespace codex_micro_hid {

bool register_service()
{
    if (!g_tx_mutex) {
        g_tx_mutex = xSemaphoreCreateMutex();
        if (!g_tx_mutex) {
            ESP_LOGE(kTag, "failed to create TX mutex");
            return false;
        }
    }
    if (!g_state_mutex) {
        g_state_mutex = xSemaphoreCreateMutex();
        if (!g_state_mutex) {
            ESP_LOGE(kTag, "failed to create state mutex");
            return false;
        }
    }
    if (!g_rx_queue) {
        g_rx_queue = xQueueCreate(24, sizeof(RxFrame));
        if (!g_rx_queue) {
            ESP_LOGE(kTag, "failed to create RPC queue");
            return false;
        }
    }
    if (!g_rpc_task_handle) {
        if (xTaskCreate(rpc_task, "codex_micro_rpc", 6 * 1024, nullptr, 3, &g_rpc_task_handle) != pdPASS) {
            ESP_LOGE(kTag, "failed to create RPC task");
            return false;
        }
    }

    int rc = ble_gatts_count_cfg(kServices);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(kServices);
    }
    if (rc != 0) {
        ESP_LOGE(kTag, "unified HID service registration failed: rc=%d", rc);
        return false;
    }
    ESP_LOGI(kTag,
             "unified HID service registered: keyboard=%u consumer=%u vendor=%u usage=0xFF00",
             kKeyboardReportId,
             kConsumerReportId,
             kReportId);
    return true;
}

void on_connected(uint16_t conn_handle)
{
    g_conn_handle = conn_handle;
    g_input_subscribed = false;
    g_warned_unsubscribed_notify = false;
    g_vendor_output_frames = 0;
    g_notify_attempts = 0;
    g_notify_successes = 0;
    g_notify_failures = 0;
    g_rpc_buffer.clear();
    reset_thread_status();
}

void on_disconnected()
{
    g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    g_input_subscribed = false;
    g_warned_unsubscribed_notify = false;
    g_rpc_buffer.clear();
    reset_thread_status();
    if (g_rx_queue) {
        xQueueReset(g_rx_queue);
    }
}

void on_gap_event(const ble_gap_event& event)
{
    if (event.type == BLE_GAP_EVENT_SUBSCRIBE && event.subscribe.attr_handle == g_input_handle) {
        const bool subscribed = event.subscribe.cur_notify != 0;
        if (g_input_subscribed != subscribed && g_state_mutex &&
            xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            ++g_thread_status_sequence;
            xSemaphoreGive(g_state_mutex);
        }
        g_input_subscribed = subscribed;
        if (subscribed) {
            g_warned_unsubscribed_notify = false;
        }
        ESP_LOGI(kTag, "Codex Micro input subscription=%d", g_input_subscribed);
    }
}

bool send_reasoning_delta(int delta)
{
    if (delta == 0 || !ready()) {
        return false;
    }

    // Codex maps ENC_CC to the next/higher effort item and ENC_CW to the
    // previous/lower item. Positive UI deltas therefore intentionally use CC.
    const char* key = delta > 0 ? "ENC_CC" : "ENC_CW";
    const int steps = std::min(8, std::abs(delta));
    const std::string json = std::string("{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"") +
                             key + "\",\"act\":2}}";
    for (int step = 0; step < steps; ++step) {
        if (!send_json(json)) {
            return false;
        }
        if (step + 1 < steps) {
            vTaskDelay(pdMS_TO_TICKS(4));
        }
    }
    ESP_LOGI(kTag, "native reasoning encoder: key=%s steps=%d", key, steps);
    return true;
}

bool tap_agent_slot(uint8_t slot)
{
    if (slot >= kThreadSlotCount || !ready()) {
        return false;
    }
    char pressed[96] = {};
    char released[96] = {};
    std::snprintf(pressed,
                  sizeof(pressed),
                  "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"AG%02u\",\"act\":1,\"ag\":%u}}",
                  static_cast<unsigned>(slot),
                  static_cast<unsigned>(slot));
    std::snprintf(released,
                  sizeof(released),
                  "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"AG%02u\",\"act\":0,\"ag\":%u}}",
                  static_cast<unsigned>(slot),
                  static_cast<unsigned>(slot));
    if (!send_json(pressed)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(24));
    return send_json(released);
}

bool send_radial(float angle, float distance)
{
    if (!ready()) {
        return false;
    }
    angle = std::clamp(angle, 0.0f, 1.0f);
    distance = std::clamp(distance, 0.0f, 1.0f);
    char json[96] = {};
    std::snprintf(json,
                  sizeof(json),
                  "{\"m\":\"v.oai.rad\",\"p\":{\"a\":%.3f,\"d\":%.3f}}",
                  static_cast<double>(angle),
                  static_cast<double>(distance));
    return send_json(json);
}

bool ready()
{
    // macOS can preserve a working HOGP notification path across a cached BLE
    // reconnect without replaying the local SUBSCRIBE event.  The cached flag
    // may therefore be false while Codex RPC requests and replies are healthy.
    // Let send_json()/notify_body() perform the authoritative live transport
    // check instead of rejecting Agent, encoder, and radial actions early.
    return g_conn_handle != BLE_HS_CONN_HANDLE_NONE && g_input_handle != 0;
}

StatusSummary status_summary()
{
    StatusSummary summary;
    summary.connected = g_conn_handle != BLE_HS_CONN_HANDLE_NONE;
    summary.ready = ready();
    if (!g_state_mutex || xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return summary;
    }
    summary.communicating = g_host_rpc_seen;
    summary.sequence = g_thread_status_sequence;
    for (const ThreadSlot& slot : g_thread_slots) {
        if (!slot.assigned) {
            continue;
        }
        ++summary.assigned;
        switch (slot.color) {
        case kWorkingColor:
            ++summary.working;
            break;
        case kUnreadColor:
            ++summary.unread;
            break;
        case kAttentionColor:
            ++summary.attention;
            break;
        case kErrorColor:
            ++summary.errors;
            break;
        default:
            break;
        }
    }
    xSemaphoreGive(g_state_mutex);
    return summary;
}

StatusSnapshot status_snapshot()
{
    StatusSnapshot snapshot;
    snapshot.connected = g_conn_handle != BLE_HS_CONN_HANDLE_NONE;
    snapshot.ready = ready();
    if (!g_state_mutex || xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return snapshot;
    }
    snapshot.communicating = g_host_rpc_seen;
    snapshot.sequence = g_thread_status_sequence;
    for (size_t index = 0; index < g_thread_slots.size(); ++index) {
        const ThreadSlot& source = g_thread_slots[index];
        AgentSlotStatus& target = snapshot.agents[index];
        target.assigned = source.assigned;
        target.color = source.color;
        target.brightness = source.brightness;
        target.effect = source.effect;
        target.speed = source.speed;
    }
    xSemaphoreGive(g_state_mutex);
    return snapshot;
}

}  // namespace codex_micro_hid
