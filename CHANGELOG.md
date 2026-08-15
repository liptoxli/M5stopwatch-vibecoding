# Changelog

本项目采用[语义化版本](https://semver.org/lang/zh-CN/)；所有公开迭代都在这里记录，并使用同版本 Git 标签发布。

## [Unreleased]

尚无。

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

[Unreleased]: https://github.com/liptoxli/M5stopwatch-vibecoding/compare/v0.6.0...HEAD
[0.6.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.6.0
