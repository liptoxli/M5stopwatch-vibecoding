# Changelog

本项目采用[语义化版本](https://semver.org/lang/zh-CN/)；所有公开迭代都在这里记录，并使用同版本 Git 标签发布。

## [Unreleased]

尚无。

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
- 新增公开构建所需的 M5IOE1/M5PM1 补丁及版本一致性检查。

### Changed

- Codex 页面改为只展示 `WEEK`，不再展示 5H 窗口。
- 额度条改为区分今日已用、此前已用和剩余额度。
- 省电策略改为 1 分钟降亮度、3 分钟 activity sleep、15 分钟无外接电源时由 PMIC 关机。
- BLE HID 输入继续由设备直接发出，Mac Bridge 仅负责状态、配置和额度摘要。
- 公开配置保留示例 URL 和 Wi-Fi 占位符，不包含私人服务或凭据。

### Fixed

- 清理未使用的 LVGL Demo 头文件依赖，使公开源码可独立构建。
- 避免公开构建依赖私有修改过的 Wi-Fi 组件 API。
- 启动音效改用 ESP-IDF 的二进制嵌入机制，避免生成文件中的本机绝对路径。

## [0.5.0] - 2026-06-13

早期固件启动页显示为 `V0.5`；这是采用正式三段式语义化版本前的首个公开版本，历史中统一记为 `0.5.0`。

### Added

- 首次公开完整 M5Stack StopWatch UserDemo 衍生固件和 macOS BLE Bridge 源码。
- 新增 Codex 圆屏页面、5 小时/周额度、Pet 动画、时间、电量和 BLE 状态。
- 新增 Typeless/微信输入法输入模式、A/B 键绑定、摇晃清除和 BLE HID 输入。
- 新增 Codex 额度 BLE 推送、隐私边界、功能说明和 Pet 替换文档。

[Unreleased]: https://github.com/liptoxli/M5stopwatch-vibecoding/compare/v0.7.0...HEAD
[0.7.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.0
[0.6.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.6.0
[0.5.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/commit/ac120b2
