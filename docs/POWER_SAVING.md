# 省电和降温策略

本项目的省电目标是降低日常桌面轻度使用时的发热和耗电，同时保留 BLE 按键、音效和 Codex/Pet 的主要体验。

## 分级空闲策略

固件在 `firmware-stopwatch-idf/main/hal/hal.cpp` 里维护 activity monitor。它每 100ms 检查一次用户活动：

- A/B 按键状态变化。
- activity sleep 状态下的触摸。
- IMU 加速度变化，阈值约 `0.12g`。

### 1 分钟：轻度省电

空闲 1 分钟后：

- 屏幕亮度降到 `10%`。
- 启用 ESP-IDF power management 的低频配置。
- CPU 频率从最高 `240MHz` 降到 `80MHz`。
- 不保存这个临时亮度，唤醒后恢复用户原亮度。

触摸、按键或移动设备后会恢复亮度和 CPU 频率。

### 3 分钟：省电模式

空闲 3 分钟后：

- 停止 Wi-Fi station 和 Wi-Fi 配网 AP。
- 停止振动。
- 停止 LVGL 更新任务。
- 背光设为 `0%`。
- AMOLED panel 进入 activity sleep。

这个阶段不是 deep sleep。设备仍在运行 activity monitor，可以通过触摸、按键或移动设备恢复。恢复时会：

- 退出屏幕 sleep。
- 重启 LVGL 更新。
- 请求全屏重绘。
- 恢复原亮度。
- 恢复 CPU 到正常配置。

### 10 分钟：Deep Sleep

空闲 10 分钟后：

- 停止 Wi-Fi。
- 停止振动。
- 停止 LVGL 更新。
- 背光设为 `0%`。
- 配置 A/B 两个物理按键作为 EXT1 唤醒源。
- 调用 `esp_deep_sleep_start()`。

Deep sleep 接近关机状态。此时触摸和移动设备不会唤醒，只有 A/B 物理按键或重新上电能恢复。恢复后等同重新启动固件，界面状态由 NVS 和启动逻辑恢复。

## Wi-Fi 策略

公开版默认：

```text
kDefaultWifiEnabled = false
kDefaultWifiQuotaFallbackEnabled = false
```

原因：

- Codex 额度优先由 macOS Bridge 通过 BLE 推送。
- Wi-Fi 扫描、连接和 HTTP fallback 会增加功耗和发热。
- 开源用户通常没有项目私有 panel 服务，默认开启 Wi-Fi fallback 反而会造成无意义重试。

需要远端 panel 的用户可以在设置页打开 Wi-Fi，并在 `codex_config.h` 填入自己的 SSID、密码和 panel URL。

## CPU 电源管理

`sdkconfig.defaults` 开启：

```text
CONFIG_PM_ENABLE=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
```

正常使用时最高 240MHz，轻度省电时切到：

```text
max_freq_mhz = 80
min_freq_mhz = 80
light_sleep_enable = false
```

这里没有启用自动 light sleep，因为屏幕、BLE、LVGL、触摸和音频链路都需要稳定验证。当前策略先用“明确的空闲状态切换”控制功耗，降低不确定性。

## 音频策略

音效保留，音频输入关闭：

```text
kEnableAudioOutput = true
kEnableAudioInput = false
```

这样仍保留开机音、按键音和提示音，但不会启用麦克风录音链路。音频输出任务在空闲时会关闭 speaker amp，避免放大器持续耗电。

## BLE 策略

BLE 是核心交互通道，默认保留：

- 接收 macOS Bridge 推送的 Codex 额度和状态。
- 接收输入模式和按键绑定配置。
- 作为 HID fallback 发送基础按键。
- 上报电池服务。

因此普通省电不会主动关闭 BLE。Deep sleep 后 BLE 会断开，唤醒重启后重新广播。

## 体验取舍

- 普通省电恢复快，适合桌面轻度使用。
- Deep sleep 更省电，但恢复等同重新启动，耗时更长。
- Wi-Fi 默认关闭，减少发热；需要远端能力时由用户显式开启。
- 音频输出保留，输入关闭，兼顾反馈音和功耗。
