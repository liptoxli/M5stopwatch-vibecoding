# Changelog

本项目采用[语义化版本](https://semver.org/lang/zh-CN/)；所有版本更新都在这里记录，并使用同版本 Git 标签发布。

## [Unreleased]

## [0.7.4] - 2026-08-16

### Changed

- 录音期间 BLE 断开或音频连续中断时，本次听写会明确标记为失败，不再自动续接可能缺失中段的语音。
- StopWatch 显示 `MIC LOST - PRESS A` 并以两次短振动提醒；BLE 重连后只恢复待机，按一次 A 即可开始全新录音。
- 空闲时的 BLE 重连继续静默进行，不显示录音异常，也不会额外启动音频采集。
- 配套 macOS Bridge 更新到 v1.1.6：断线时保持虚拟输入和系统默认输入不变，结束当前 Typeless 听写并保留已经识别的文字。
- Bridge 增加录音流停顿和连续丢帧检测；异常状态会保留在菜单栏和设备界面，直到用户主动重试。

### Fixed

- 修复录音中途 BLE 重连后设备停在待机、Typeless 却继续录制静音，最终只得到部分文字的问题。

## [0.7.3] - 2026-08-16

### Changed

- 虚拟麦克风开启后继续保持 BLE、HID、额度和状态连接，但空闲时停止 PCM 采集、ADPCM 编码和音频通知。
- 设备 A 键在发送 HID/Bridge 事件前先本地启动麦克风；停止录音后保留 400ms 音频尾段再回到待机。
- 空闲音频任务由每 10ms 轮询改为阻塞等待，仅每秒唤醒一次发送健康统计。
- 配套 macOS Bridge 更新到 v1.1.5：支持按需音频控制，并监控原 Typeless 快捷键，在使用 Mac 键盘启动听写时也能提前唤醒 StopWatch 麦克风。
- BLE 连接继续使用已验证的 15ms 间隔，本版本不调整连接参数、配对、HID 或自动关机逻辑。

### Compatibility

- Bridge 会探测固件是否支持按需音频；旧固件拒绝新控制命令时自动回退为原来的连续音频流。

## [0.7.2] - 2026-08-16

### Changed

- 第二套 UI 保持现有布局与功能，将动态刷新降为 10 FPS；第一套 UI 仍保持原有 20/30 FPS 节奏。
- 静态额度画布只在周额度或活动方格变化时重绘，文字只在内容变化时更新，录音波形取消阴影并跳过重复帧。
- 同步状态装饰改为独立的小面积控件，录音状态切换不再触发整张 466×466 画布刷新。
- 配套 macOS Bridge 更新到 v1.1.4：录音状态继续实时同步，但不再连带推送完整额度面板；额度仍按连接和定时机制更新。

### Fixed

- 第二套 UI 更新为当前已选布局，并统一刷新与按键响应逻辑。

## [0.7.1] - 2026-08-16

### Fixed

- OpenWatcher V2 录音动画不再每 33 ms 重绘整张 466×466 静态画布，避免录音开始后 A/B 按键失去响应。
- 配套 macOS Bridge 更新到 v1.1.3：Typeless 模式恢复 A 键按下切换录音，松开不再立即停止；Processing 状态下再次按 A 会直接开始新录音。

## [0.7.0] - 2026-08-16

### Added

- 在完整 StopWatch 固件上叠加实时 BLE 麦克风服务，保留原 UI、Codex、额度同步与 BLE HID 按键。
- 新增 16 kHz、20 ms 分帧的 4-bit IMA-ADPCM 音频协议，稳态 BLE 载荷约 8.7 KB/s。
- macOS Bridge v1.1.0 新增 `M5 StopWatch Mic` 菜单开关、默认输入切换与恢复、ADPCM 解码和丢包静音补齐。
- 新增可复现构建的 `M5 StopWatch Mic` Core Audio 虚拟麦克风驱动，作为独立 GPLv3 组件管理。

### Changed

- 麦克风模式默认关闭；开启时连续实时传输，不录制或上传 WAV 文件。
- 固件和已配对 Mac 继续共用同一条 BLE 连接；音频服务不启动第二个蓝牙栈。
- 从旧版分区布局首次升级到 v0.7.0 时必须执行完整 `idf.py flash`；后续在分区布局不变时才可使用 `idf.py app-flash`。

### Fixed

- 记录并修正旧 Bootloader/分区表与 5.5 MB 新应用不匹配导致的启动黑屏问题。
- 完整刷写后如旧 BLE 配对密钥失效，需在 macOS 忽略 `M5Codex-*` 后重新配对。

### Verified

- 实机完成 `StopWatch 麦克风 -> BLE -> Mac Bridge -> M5 StopWatch Mic -> Typeless` 端到端语音识别。
- 16 kHz 实时流连续发送 3,118 个 20 ms 音频包，验收区间丢包为 0。

## [0.6.0] - 2026-08-15

### Added

- 新增 Codex 周额度圆屏半圆几何和 08:00 日统计基线。
- 新增 macOS Bridge 的输入模式、按键绑定、状态同步和看门狗安装支持。
- 新增设置页中的 Bluetooth、Wi-Fi 和额度推送开关。
- 新增 M5IOE1/M5PM1 兼容补丁及版本一致性检查。

### Changed

- Codex 页面改为只展示 `WEEK`，不再展示 5H 窗口。
- 额度条改为区分今日已用、此前已用和剩余额度。
- 省电策略改为 1 分钟降亮度、3 分钟 activity sleep、15 分钟无外接电源时由 PMIC 关机。
- BLE HID 输入继续由设备直接发出，Mac Bridge 仅负责状态、配置和额度摘要。
- 默认配置使用示例 URL 和 Wi-Fi 占位符，不包含用户凭据。

### Fixed

- 清理未使用的 LVGL Demo 头文件依赖，使源码可独立构建。
- 修复固件构建对非标准 Wi-Fi 组件 API 的依赖。
- 启动音效改用 ESP-IDF 的二进制嵌入机制，避免生成文件中的本机绝对路径。

## [0.5.0] - 2026-06-13

早期固件启动页显示为 `V0.5`；这是采用正式三段式语义化版本前的首个版本，历史中统一记为 `0.5.0`。

### Added

- 发布 M5Stack StopWatch 固件和 macOS BLE Bridge 源码。
- 新增 Codex 圆屏页面、5 小时/周额度、Pet 动画、时间、电量和 BLE 状态。
- 新增 Typeless/微信输入法输入模式、A/B 键绑定、摇晃清除和 BLE HID 输入。
- 新增 Codex 额度 BLE 推送、隐私与安全说明、功能说明和 Pet 替换文档。

[Unreleased]: https://github.com/liptoxli/M5stopwatch-vibecoding/compare/v0.7.4...HEAD
[0.7.4]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.4
[0.7.3]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.3
[0.7.2]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.2
[0.7.1]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.1
[0.7.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.0
[0.6.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.6.0
[0.5.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/commit/ac120b2
