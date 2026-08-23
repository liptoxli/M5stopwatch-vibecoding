#include "ble_microphone.h"

#include "hal.h"
#include "ima_adpcm.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <vector>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <host/ble_att.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_esp_gap.h>
#include <host/ble_uuid.h>
#include <os/os_mbuf.h>

namespace {

constexpr char kTag[] = "BLE-Mic";
constexpr std::uint32_t kStreamSampleRate = 16000;
constexpr std::uint8_t kCodecImaAdpcm = 1;
constexpr std::size_t kPacketHeaderSize = 14;
constexpr std::size_t kPacketSize = kPacketHeaderSize + ima_adpcm::kEncodedBytesPerBlock;
constexpr std::uint32_t kStopTailMs = 400;
constexpr std::uint32_t kPendingVoiceTimeoutMs = 6000;

enum class StreamMode : std::uint8_t {
    Disabled = 0,
    Continuous = 1,
    Armed = 2,
};

const ble_uuid128_t kServiceUuid = BLE_UUID128_INIT(
    0x01, 0xb0, 0xa1, 0xf6, 0xc8, 0xa8, 0x42, 0x9f, 0x3c, 0x4f, 0xf1, 0x5c, 0x01, 0x00, 0x2f, 0x7d);
const ble_uuid128_t kControlUuid = BLE_UUID128_INIT(
    0x01, 0xb0, 0xa1, 0xf6, 0xc8, 0xa8, 0x42, 0x9f, 0x3c, 0x4f, 0xf1, 0x5c, 0x02, 0x00, 0x2f, 0x7d);
const ble_uuid128_t kAudioUuid = BLE_UUID128_INIT(
    0x01, 0xb0, 0xa1, 0xf6, 0xc8, 0xa8, 0x42, 0x9f, 0x3c, 0x4f, 0xf1, 0x5c, 0x03, 0x00, 0x2f, 0x7d);
const ble_uuid128_t kStatsUuid = BLE_UUID128_INIT(
    0x01, 0xb0, 0xa1, 0xf6, 0xc8, 0xa8, 0x42, 0x9f, 0x3c, 0x4f, 0xf1, 0x5c, 0x04, 0x00, 0x2f, 0x7d);

std::atomic<bool> g_connected = false;
std::atomic<bool> g_audio_subscribed = false;
std::atomic<bool> g_stats_subscribed = false;
std::atomic<StreamMode> g_stream_mode = StreamMode::Disabled;
std::atomic<bool> g_voice_intent = false;
std::atomic<bool> g_voice_requested = false;
std::atomic<bool> g_voice_session_interrupted = false;
std::atomic<bool> g_voice_start_timeout_pending = false;
std::atomic<TickType_t> g_voice_intent_started_at = 0;
std::atomic<TickType_t> g_stop_at_tick = 0;
std::atomic<std::uint32_t> g_stream_generation = 0;
std::atomic<bool> g_capture_task_started = false;
std::atomic<bool> g_link_update_task_running = false;
std::atomic<std::uint8_t> g_link_update_attempts = 0;
std::atomic<bool> g_audio_input_held = false;
std::atomic<std::uint32_t> g_packets_sent = 0;
std::atomic<std::uint32_t> g_packets_dropped = 0;
std::atomic<std::uint32_t> g_pcm_bytes_sent = 0;

std::uint16_t g_connection_handle = BLE_HS_CONN_HANDLE_NONE;
std::uint16_t g_audio_handle = 0;
std::uint16_t g_stats_handle = 0;
std::uint16_t g_sequence = 0;
std::uint32_t g_sample_index = 0;
TickType_t g_last_stats_tick = 0;
TaskHandle_t g_capture_task_handle = nullptr;
ima_adpcm::Encoder g_encoder;

void schedule_link_update();

bool acquire_stream_audio()
{
    bool expected = false;
    if (!g_audio_input_held.compare_exchange_strong(expected, true)) {
        return true;
    }
    if (GetHAL().acquireAudioInput()) {
        return true;
    }
    g_audio_input_held = false;
    ESP_LOGE(kTag, "audio input wake failed");
    return false;
}

void release_stream_audio()
{
    bool expected = true;
    if (g_audio_input_held.compare_exchange_strong(expected, false)) {
        GetHAL().releaseAudioInput();
    }
}

void wake_capture_task()
{
    if (g_capture_task_handle != nullptr) {
        xTaskNotifyGive(g_capture_task_handle);
    }
}

void start_latched_voice_if_ready()
{
    if (!g_voice_intent.load() || g_stream_mode.load() != StreamMode::Armed ||
        !g_connected.load() || !g_audio_subscribed.load()) {
        return;
    }
    g_stop_at_tick = 0;
    g_voice_intent_started_at = 0;
    g_voice_session_interrupted = false;
    if (!g_voice_requested.exchange(true)) {
        ++g_stream_generation;
        ESP_LOGI(kTag, "latched voice request started after audio path became ready");
    }
    wake_capture_task();
}

void request_voice_start()
{
    if (!g_voice_intent.exchange(true)) {
        g_voice_intent_started_at = xTaskGetTickCount();
        g_voice_start_timeout_pending = false;
        ESP_LOGI(kTag, "voice intent latched: connected=%d subscribed=%d mode=%u",
                 g_connected.load(),
                 g_audio_subscribed.load(),
                 static_cast<unsigned>(g_stream_mode.load()));
    }
    start_latched_voice_if_ready();
}

void stop_voice_stream(bool immediate)
{
    if (!g_voice_requested.load()) {
        g_stop_at_tick = 0;
        return;
    }
    if (immediate) {
        g_stop_at_tick = 0;
        g_voice_requested = false;
        wake_capture_task();
        return;
    }
    g_stop_at_tick = xTaskGetTickCount() + pdMS_TO_TICKS(kStopTailMs);
}

void request_voice_stop(bool immediate)
{
    g_voice_intent = false;
    g_voice_intent_started_at = 0;
    stop_voice_stream(immediate);
}

void apply_voice_start_timeout()
{
    const TickType_t started_at = g_voice_intent_started_at.load();
    if (!g_voice_intent.load() || g_voice_requested.load() || started_at == 0) {
        return;
    }
    if (xTaskGetTickCount() - started_at < pdMS_TO_TICKS(kPendingVoiceTimeoutMs)) {
        return;
    }

    g_voice_intent = false;
    g_voice_intent_started_at = 0;
    g_voice_start_timeout_pending = true;
    g_voice_session_interrupted = true;
    ESP_LOGE(kTag,
             "voice start timed out waiting for audio path: connected=%d subscribed=%d mode=%u",
             g_connected.load(),
             g_audio_subscribed.load(),
             static_cast<unsigned>(g_stream_mode.load()));
}

void apply_stop_deadline()
{
    const TickType_t deadline = g_stop_at_tick.load();
    if (deadline != 0 && static_cast<std::int32_t>(xTaskGetTickCount() - deadline) >= 0) {
        g_stop_at_tick = 0;
        g_voice_requested = false;
    }
}

void put_u16(std::uint8_t* target, std::uint16_t value)
{
    target[0] = static_cast<std::uint8_t>(value & 0xff);
    target[1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

void put_u32(std::uint8_t* target, std::uint32_t value)
{
    target[0] = static_cast<std::uint8_t>(value & 0xff);
    target[1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    target[2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    target[3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

std::array<std::uint8_t, 20> make_stats()
{
    std::array<std::uint8_t, 20> stats = {};
    stats[0] = 3;
    stats[1] = ble_microphone::is_streaming() ? 1 : 0;
    stats[2] = g_audio_subscribed.load() ? 1 : 0;
    put_u32(&stats[4], kStreamSampleRate);
    put_u32(&stats[8], g_packets_sent.load());
    put_u32(&stats[12], g_packets_dropped.load());
    put_u32(&stats[16], g_pcm_bytes_sent.load());
    return stats;
}

int notify_once(std::uint16_t handle, const std::uint8_t* bytes, std::size_t length)
{
    if (!g_connected.load() || handle == 0 || g_connection_handle == BLE_HS_CONN_HANDLE_NONE) {
        return BLE_HS_ENOTCONN;
    }
    os_mbuf* packet = ble_hs_mbuf_from_flat(bytes, length);
    return packet == nullptr ? BLE_HS_ENOMEM : ble_gatts_notify_custom(g_connection_handle, handle, packet);
}

void poll_stats()
{
    const TickType_t now = xTaskGetTickCount();
    if (!g_stats_subscribed.load() || now - g_last_stats_tick < pdMS_TO_TICKS(1000)) {
        return;
    }
    g_last_stats_tick = now;
    const auto stats = make_stats();
    notify_once(g_stats_handle, stats.data(), stats.size());
}

bool send_adpcm(const std::int16_t* samples)
{
    if (!ble_microphone::is_streaming() || ble_att_mtu(g_connection_handle) < kPacketSize + 3) {
        return false;
    }

    ima_adpcm::Block block;
    if (!g_encoder.encode(samples, ima_adpcm::kSamplesPerBlock, block)) {
        return false;
    }

    std::array<std::uint8_t, kPacketSize> packet = {};
    put_u16(&packet[0], g_sequence++);
    packet[2] = kCodecImaAdpcm;
    put_u32(&packet[4], g_sample_index);
    put_u16(&packet[8], ima_adpcm::kSamplesPerBlock);
    put_u16(&packet[10], static_cast<std::uint16_t>(block.predictor));
    packet[12] = block.step_index;
    std::memcpy(&packet[kPacketHeaderSize], block.encoded.data(), block.encoded.size());

    const TickType_t retry_start = xTaskGetTickCount();
    int result = BLE_HS_EAGAIN;
    do {
        result = notify_once(g_audio_handle, packet.data(), packet.size());
        if (result == 0) {
            break;
        }
        const bool queue_full = result == BLE_HS_EAGAIN || result == BLE_HS_ENOMEM || result == BLE_HS_EBUSY;
        if (!queue_full || xTaskGetTickCount() - retry_start >= pdMS_TO_TICKS(10)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (ble_microphone::is_streaming());

    if (result == 0) {
        ++g_packets_sent;
        g_pcm_bytes_sent += ima_adpcm::kSamplesPerBlock * sizeof(std::int16_t);
    } else {
        ++g_packets_dropped;
    }
    g_sample_index += ima_adpcm::kSamplesPerBlock;
    return result == 0;
}

void resample_to_16khz(const std::vector<std::int16_t>& source,
                       std::array<std::int16_t, ima_adpcm::kSamplesPerBlock>& target)
{
    if (source.empty()) {
        target.fill(0);
        return;
    }
    for (std::size_t i = 0; i < target.size(); ++i) {
        const std::size_t source_index = std::min(source.size() - 1, (i * source.size()) / target.size());
        target[i] = source[source_index];
    }
}

void capture_task(void*)
{
    std::vector<std::int16_t> source;
    std::array<std::int16_t, ima_adpcm::kSamplesPerBlock> frame = {};
    std::uint32_t active_generation = 0;
    while (true) {
        apply_voice_start_timeout();
        apply_stop_deadline();
        if (!ble_microphone::is_streaming()) {
            release_stream_audio();
            poll_stats();
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
            continue;
        }
        // Codec/I2S wake-up may take time and must never run inside the
        // NimBLE GATT callback. Keep all hardware work on the capture task.
        if (!acquire_stream_audio()) {
            g_voice_session_interrupted = true;
            g_voice_requested = false;
            if (g_stream_mode.load() == StreamMode::Continuous) {
                g_stream_mode = StreamMode::Disabled;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        const std::uint32_t generation = g_stream_generation.load();
        if (generation != active_generation) {
            active_generation = generation;
            g_sequence = 0;
            g_sample_index = 0;
            g_encoder.reset();
            g_packets_sent = 0;
            g_packets_dropped = 0;
            g_pcm_bytes_sent = 0;
        }
        GetHAL().audioRecord(source, 20, 30.0f);
        if (source.empty()) {
            ESP_LOGW(kTag, "microphone read returned no samples");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        resample_to_16khz(source, frame);
        send_adpcm(frame.data());
        poll_stats();
    }
}

void link_update_task(void*)
{
    // Use the v0.7.x stable profile for the full connection. macOS already
    // treats the device as a low-latency BLE HID; changing 30/15 ms on every
    // voice session creates competing connection-parameter updates.
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (!g_connected.load() || g_connection_handle == BLE_HS_CONN_HANDLE_NONE) {
        g_link_update_task_running = false;
        vTaskDelete(nullptr);
        return;
    }
    ble_gap_upd_params params = {};
    params.itvl_min = 12;
    params.itvl_max = 12;
    params.latency = 0;
    params.supervision_timeout = 400;
    const std::uint8_t attempt = ++g_link_update_attempts;
    const int result = ble_gap_update_params(g_connection_handle, &params);
    ESP_LOGI(kTag, "15 ms interval request #%u: %d", attempt, result);
    g_link_update_task_running = false;
    if (result != 0 && attempt < 3) {
        schedule_link_update();
    }
    vTaskDelete(nullptr);
}

void schedule_link_update()
{
    bool expected = false;
    if (!g_link_update_task_running.compare_exchange_strong(expected, true)) {
        return;
    }
    if (xTaskCreate(link_update_task, "ble_mic_link", 3072, nullptr, 5, nullptr) != pdPASS) {
        g_link_update_task_running = false;
    }
}

int gatt_access(std::uint16_t, std::uint16_t, ble_gatt_access_ctxt* context, void*)
{
    if (ble_uuid_cmp(context->chr->uuid, &kControlUuid.u) == 0 &&
        context->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        std::uint8_t command = 0;
        std::uint16_t copied = 0;
        const int result = ble_hs_mbuf_to_flat(context->om, &command, sizeof(command), &copied);
        if (result != 0 || copied != 1 || command > 4) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        switch (command) {
        case 0:
            g_stream_mode = StreamMode::Disabled;
            request_voice_stop(true);
            break;
        case 1:
            g_stream_mode = StreamMode::Continuous;
            g_voice_intent = false;
            g_voice_intent_started_at = 0;
            g_stop_at_tick = 0;
            g_voice_requested = false;
            ++g_stream_generation;
            wake_capture_task();
            break;
        case 2:
            g_stream_mode = StreamMode::Armed;
            stop_voice_stream(true);
            start_latched_voice_if_ready();
            break;
        case 3:
            request_voice_start();
            break;
        case 4:
            request_voice_stop(false);
            break;
        default:
            break;
        }
        ESP_LOGI(kTag, "control command=%u streaming=%s", command,
                 ble_microphone::is_streaming() ? "true" : "false");
        return 0;
    }
    if (ble_uuid_cmp(context->chr->uuid, &kStatsUuid.u) == 0 &&
        context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const auto stats = make_stats();
        return os_mbuf_append(context->om, stats.data(), stats.size()) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

const ble_gatt_chr_def kCharacteristics[] = {
    {
        .uuid = &kControlUuid.u,
        .access_cb = gatt_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        .min_key_size = 0,
        .val_handle = nullptr,
        .cpfd = nullptr,
    },
    {
        .uuid = &kAudioUuid.u,
        .access_cb = gatt_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_audio_handle,
        .cpfd = nullptr,
    },
    {
        .uuid = &kStatsUuid.u,
        .access_cb = gatt_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_stats_handle,
        .cpfd = nullptr,
    },
    {},
};

const ble_gatt_svc_def kServices[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kServiceUuid.u,
        .includes = nullptr,
        .characteristics = kCharacteristics,
    },
    {},
};

}  // namespace

namespace ble_microphone {

bool register_service()
{
    ble_att_set_preferred_mtu(247);
    int result = ble_gatts_count_cfg(kServices);
    if (result == 0) {
        result = ble_gatts_add_svcs(kServices);
    }
    if (result != 0) {
        ESP_LOGE(kTag, "register audio service failed: %d", result);
    }
    return result == 0;
}

void start_capture_task()
{
    bool expected = false;
    if (!g_capture_task_started.compare_exchange_strong(expected, true)) {
        return;
    }
    if (xTaskCreatePinnedToCore(capture_task, "ble_mic_capture", 8192, nullptr, 8,
                                &g_capture_task_handle, 1) != pdPASS) {
        g_capture_task_started = false;
        g_capture_task_handle = nullptr;
        ESP_LOGE(kTag, "failed to create capture task");
    }
}

void on_connected(std::uint16_t connection_handle)
{
    g_connected = true;
    g_connection_handle = connection_handle;
    g_link_update_attempts = 0;
    schedule_link_update();
    const int phy_result = ble_gap_set_prefered_le_phy(connection_handle,
                                                       BLE_GAP_LE_PHY_2M_MASK,
                                                       BLE_GAP_LE_PHY_2M_MASK,
                                                       BLE_GAP_LE_PHY_CODED_ANY);
    const int length_result = ble_hs_hci_util_set_data_len(connection_handle, 251, 2120);
    ESP_LOGI(kTag, "link requests: phy=%d data_length=%d", phy_result, length_result);
}

void on_disconnected()
{
    if ((g_voice_intent.load() || g_voice_requested.load()) && g_stop_at_tick.load() == 0) {
        g_voice_session_interrupted = true;
    }
    g_connected = false;
    g_audio_subscribed = false;
    g_stats_subscribed = false;
    g_stream_mode = StreamMode::Disabled;
    g_voice_intent = false;
    g_voice_requested = false;
    g_voice_intent_started_at = 0;
    g_voice_start_timeout_pending = false;
    g_stop_at_tick = 0;
    g_connection_handle = BLE_HS_CONN_HANDLE_NONE;
    // Do not touch Codec/I2S from the NimBLE GAP callback. The capture task
    // observes the disconnected state and releases audio asynchronously.
    wake_capture_task();
}

void on_gap_event(const ble_gap_event& event)
{
    if (event.type == BLE_GAP_EVENT_SUBSCRIBE) {
        if (event.subscribe.attr_handle == g_audio_handle) {
            g_audio_subscribed = event.subscribe.cur_notify != 0;
            ESP_LOGI(kTag, "audio notifications: %s", g_audio_subscribed ? "on" : "off");
            if (g_audio_subscribed.load()) {
                start_latched_voice_if_ready();
            }
        } else if (event.subscribe.attr_handle == g_stats_handle) {
            g_stats_subscribed = event.subscribe.cur_notify != 0;
        }
    } else if (event.type == BLE_GAP_EVENT_CONN_UPDATE) {
        ESP_LOGI(kTag, "connection update status: %d", event.conn_update.status);
        if (event.conn_update.status != 0 && g_link_update_attempts.load() < 3) {
            schedule_link_update();
        }
    } else if (event.type == BLE_GAP_EVENT_PHY_UPDATE_COMPLETE) {
        ESP_LOGI(kTag, "PHY update: status=%d tx=%u rx=%u",
                 event.phy_updated.status,
                 event.phy_updated.tx_phy,
                 event.phy_updated.rx_phy);
    }
}

bool is_streaming()
{
    if (!g_connected.load() || !g_audio_subscribed.load()) {
        return false;
    }
    const StreamMode mode = g_stream_mode.load();
    return mode == StreamMode::Continuous || (mode == StreamMode::Armed && g_voice_requested.load());
}

void begin_voice_input()
{
    request_voice_start();
}

void end_voice_input()
{
    request_voice_stop(false);
}

bool voice_session_interrupted()
{
    return g_voice_session_interrupted.load();
}

bool consume_voice_start_timeout()
{
    return g_voice_start_timeout_pending.exchange(false);
}

void clear_voice_session_interruption()
{
    g_voice_session_interrupted = false;
}

}  // namespace ble_microphone
