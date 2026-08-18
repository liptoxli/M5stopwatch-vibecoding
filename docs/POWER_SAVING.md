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
- CPU 空闲基线从 `160MHz` 降到 `80MHz`。录音或 FFT 持有性能锁时仍可自动升到 `240MHz`。
- 不保存这个临时亮度，唤醒后恢复用户原亮度。

触摸、按键或移动设备后会恢复亮度和 CPU 频率。

### 3 分钟：省电模式

空闲 3 分钟后：

- 停止 Wi-Fi station 和 Wi-Fi 配网 AP。
- 停止振动。
- 停止 LVGL 更新任务。
- 停止 LVGL `10ms` tick timer，不再只是跳过绘制。
- 背光设为 `0%`。
- AMOLED panel 进入 activity sleep。

这个阶段不是 deep sleep。设备仍在运行 activity monitor，可以通过触摸、按键或移动设备恢复。恢复时会：

- 退出屏幕 sleep。
- 重启 LVGL 更新。
- 请求全屏重绘。
- 恢复原亮度。
- 恢复 CPU 到正常配置。

### 15 分钟：PMIC 自动关机

空闲 15 分钟且未接外部电源时：

- 停止 Wi-Fi。
- 停止振动。
- 停止 LVGL 更新。
- 背光设为 `0%`。
- 让 AMOLED panel 进入 sleep。
- 调用 PMIC shutdown，切断主系统供电并保留 RTC 电源域。

这是实际关机，不是 ESP deep sleep。恢复后等同重新启动固件，界面状态由 NVS 和启动逻辑恢复。检测到 USB 等外部电源时，15 分钟自动关机会被跳过，但 1 分钟降亮度和 3 分钟 activity sleep 仍然生效。

## Wi-Fi 策略

默认配置：

```text
kDefaultWifiEnabled = false
kDefaultWifiQuotaFallbackEnabled = false
```

原因：

- Codex 额度优先由 macOS Bridge 通过 BLE 推送。
- Wi-Fi 扫描、连接和 HTTP fallback 会增加功耗和发热。
- 未配置 panel 服务时，开启 Wi-Fi fallback 会造成无意义重试。

需要远端 panel 的用户可以在设置页打开 Wi-Fi，并在 `codex_config.h` 填入自己的 SSID、密码和 panel URL。

## CPU 电源管理

`sdkconfig.defaults` 和发布用 `sdkconfig` 都开启：

```text
CONFIG_PM_ENABLE=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
```

正常使用时：

```text
max_freq_mhz = 240
min_freq_mhz = 160
light_sleep_enable = false
```

轻度省电和 activity sleep 时：

```text
max_freq_mhz = 240
min_freq_mhz = 80
light_sleep_enable = false
```

麦克风采集和 FFT 会显式持有 `ESP_PM_CPU_FREQ_MAX` 锁，因此长录音进入 1 分钟变暗状态后仍保持 240MHz。停止采集后立即释放锁。

这里没有启用自动 light sleep，因为屏幕、BLE、LVGL、触摸和音频链路都需要稳定验证。当前策略先用“明确的空闲状态切换”控制功耗，降低不确定性。

已有 `sdkconfig` 会覆盖 `sdkconfig.defaults`；构建时应确认最终生成的 `sdkconfig` 中 `CONFIG_PM_ENABLE=y`，否则 1 分钟阶段只会降低屏幕亮度，CPU 降频代码不会生效。

## 音频策略

音效和 StopWatch 麦克风都保留：

```text
kEnableAudioOutput = true
kEnableAudioInput = true
```

音频输出任务在空闲时会关闭 speaker amp。开启 macOS 菜单中的 `M5 StopWatch Mic` 后，虚拟输入设备和 BLE 连接保持就绪；只有开始语音输入时才唤醒 ES8311 Codec 和 I2S，采集 44.1kHz 输入、压缩为 16kHz IMA-ADPCM，并以 20ms 一帧通过 BLE 发送。

A 键开始录音时会先同步恢复 Codec/I2S，再保留 20ms 稳定时间，然后继续原有 Typeless/HID 触发。停止后保留 400ms 尾段，再释放音频输入引用；没有提示音播放时，Codec 和 I2S 整体进入 suspend。

## BLE 策略

BLE 是核心交互通道，默认保留：

- 接收 macOS Bridge 推送的 Codex 额度和状态。
- 接收输入模式和按键绑定配置。
- 作为 HID fallback 发送基础按键。
- 上报电池服务。

因此 activity sleep 不会主动关闭 BLE。PMIC 关机后 BLE 会断开，重新开机后再次广播。

BLE 连接固定使用已经通过长内容录音验证的 15ms 间隔。v0.8.1 取消了空闲 30ms / 录音 15ms 的动态切换，避免连接参数更新和面板写入叠加时出现 ATT 超时。省电主要通过停止空闲音频采集、编码和通知实现，不通过反复调整 BLE 连接参数实现。

空闲时 BLE、HID、额度和状态连接仍保持，但音频包停止；AMOLED 在 3 分钟阶段关闭后，A 键仍可唤醒屏幕并本地启动音频。

## 循环、LVGL 和 IMU

- 主循环调度间隔由 5ms 调整为 10ms，实体按键新增的最坏等待约 5ms。
- LVGL 使用 `lv_timer_handler()` 返回的下次时间，在 5–50ms 之间自适应唤醒；activity sleep 时任务改为 100ms 检查且 tick timer 完全停止。
- IMU 硬件仍保留加速度计和陀螺仪、摇晃和移动唤醒功能；HAL 在 50ms 窗口内共享最新采样，不再因多个界面重复读 I2C。

## 电池审计

拔掉外部电源后，固件仍每 60 秒保留一个电压样本，且每秒累计：

- CPU 位于 80/160/240MHz 的样本占比。
- 麦克风/FFT 音频输入活跃占比。
- AMOLED activity sleep 占比。

测试完成后重新插入 USB，固件会在第 5 秒和第 25 秒重复输出内存中的审计结果，方便手动打开串口监视。这些样本不写入 Flash，不会增加 NVS 磨损。

固件 v0.7.2 已把第二套 UI 的动态刷新降到 10 FPS，并将静态额度画布、文字和录音波形改为差分更新；v0.7.3 进一步在空闲时停止音频采集、编码和通知。

固件 v0.7.4 在录音链路异常时使用静态错误提示和两次短振动，不持续播放动画，也不会在 BLE 重连后自动恢复音频流；只有用户按 A 重试时才重新启动采集和编码。

固件 v0.9.0 继续使用变化驱动的界面更新：Codex 未读数量只在变化时同步，四小时活动热力图只统计实际录音时长、启动频率和少量实体交互，不让空闲或额度同步持续触发高亮和重绘。

## 实测续航

2026-08-17 至 2026-08-18 的一轮真实混合使用测试累计开机 **4 小时 33 分 55 秒**。设备不是从 100% 开始；从首次可靠的 86% 读数到插入 USB 的累计开机时间为 **4 小时 20 分 51 秒**。最后阶段包含屏幕常亮和频繁录音，按相同负载可粗略理解为完整电量 **约 5 小时级**。

表显 0% 后设备仍运行至少 2 分 56 秒，说明当前线性电压映射在低电量区偏保守。完整区间和限制见 [续航测试记录](BATTERY_TEST.md)。

## 体验取舍

- 普通省电恢复快，适合桌面轻度使用。
- PMIC 自动关机是最终兜底，恢复等同重新启动，耗时更长。
- Wi-Fi 默认关闭，减少发热；需要远端能力时由用户显式开启。
- 音频输出保留；麦克风输入按需启动，兼顾反馈音、语音输入和功耗。
