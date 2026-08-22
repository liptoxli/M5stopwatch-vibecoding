# StopWatch BLE Bridge Changelog

Mac Bridge 使用独立语义化版本。固件与 Bridge 的兼容版本关系见仓库根目录 [README](../../README.md#版本历史)。

## [1.3.2] - 2026-08-22

### Changed

- macOS 原生 IOHID 设备出现后等待 0.7 秒，再由 CoreBluetooth 接入 Companion 和麦克风服务，避免原生 HID 与 Bridge 在连接交接瞬间竞争。
- 认证尚未就绪时每 1.2 秒重试订阅，最多三次；达到上限后停止自动重试并显示需要重置配对，不再持续触发系统认证提示。
- 扫描等待日志按 10 秒限频，避免长期断开时日志文件快速增长。

### Fixed

- 修复 Bridge 健康检查从系统已连接列表取回设备后，未确认原生 HID 状态便直接连接的问题。
- 修复 event stream 或麦克风订阅返回 `Authentication is insufficient` 后没有恢复策略的问题。

### Verified

- Bridge 本地 Swift 构建、稳定签名安装和 LaunchAgent 重载成功。
- 固件连续重启后 Bridge 均自动恢复，近期日志无认证失败，虚拟麦克风管线与 BLE 订阅状态正常。

## [1.3.1] - 2026-08-21

### Changed

- 扫描到 `M5Codex-*` 后先通过 IOHID 确认 macOS 原生键盘链路已经连接，再接入 Companion 和音频服务，避免 CoreBluetooth 与系统 HOGP 配对同时抢占外设。
- 每次新录音开始前检查虚拟输入路由；连续录音保持热启动，空闲超过 8 秒或输出不健康时才重建 Core Audio 输出。
- BLE 断开时完整停止旧音频输出，重连后不再复用可能只输出静音的 AVAudioEngine。

### Fixed

- 修复首次配对需要反复尝试，或者已经连接后仍反复出现认证提示的问题。
- 修复菜单仍显示麦克风已启用、系统也能看到 `M5 StopWatch Mic`，但重连或空闲后的首次录音没有声纹和音频的问题。

### Compatibility

- 不修改 16 kHz IMA-ADPCM 格式、虚拟麦克风驱动、按键、Agent、推理等级或中心四向协议。

## [1.3.0] - 2026-08-20

### Added

- 读取本机 Codex 当前推理等级并随额度面板同步到 StopWatch。
- 支持设备请求打开指定 Codex task，并为后续任务列表入口提供安全的 thread ID 校验和 `codex://threads/` 路由。
- 支持推理等级回退队列：原生 Codex Micro Vendor HID 暂时不可用时，通过 Codex 命令面板逐级执行并在结束后重新同步确认状态。

### Changed

- BLE 重连时发现统一 HID 服务内的所有可通知 Report characteristic，辅助恢复 Codex Vendor Report 订阅。
- 未读 task 同步扩展为可分片的 task 摘要协议；主界面仍优先显示四个原生 Agent 槽位。

### Fixed

- 修复 BLE 超时重连后标准键盘 Report 已恢复、Codex Vendor Report 仍未订阅的问题。
- 修复连续调整推理等级时多条回退命令可能并发进入 Codex 命令面板的问题。

### Compatibility

- 不修改 16 kHz IMA-ADPCM 音频格式、虚拟麦克风驱动和原有 A/B 键配置。

## [1.2.0] - 2026-08-18

### Added

- 读取本机 Codex 未读任务状态，过滤已归档的本地任务，并向固件发送轻量 `codex_unread` 数量消息。
- 活动统计持久化到本机应用支持目录，Bridge 重启后可恢复最近四小时内已经完成的录音区间。

### Changed

- 最近四小时活动强度由离散事件权重改为“70% 实际录音时长 + 30% 启动频率”，避免短暂事件直接显示为重度使用。
- 每个 10 分钟方格以四次录音启动作为频率参考；按键和摇晃只保留很小的交互权重。
- 未读数量和活动面板继续复用串行 GATT 队列，只在数据变化时发送，不增加持续 BLE 流量。

### Compatibility

- 不修改 16 kHz IMA-ADPCM 音频格式、虚拟麦克风驱动、BLE HID 或现有按键配置。

## [1.1.8] - 2026-08-17

### Changed

- 麦克风控制、状态同步和额度面板共用一个有优先级的 GATT 写入队列；录音开始/停止命令优先于普通状态和面板更新。
- 额度面板分片改为等待上一条 `write-with-response` 完成后再发送，避免多个 ATT 请求重叠。
- 完整安装流程会同步安装 60 秒进程守护，Bridge 异常退出后自动重新启动。

### Fixed

- 修复录音控制与额度面板并发写入时出现 ATT 响应超时、音频停顿和设备断线的问题。

### Compatibility

- 不修改 BLE characteristic、16 kHz IMA-ADPCM 音频格式、虚拟麦克风驱动和输入模式配置。

## [1.1.7] - 2026-08-16

### Changed

- Codex 活动方格统计窗口从最近 2 小时扩展为最近 4 小时，继续使用 24 格，每格代表 10 分钟。
- 面板数据中的 `activity_window` 和空闲标签同步更新为 `4h`。

### Compatibility

- 不修改 BLE 音频、按键、虚拟麦克风或额度同步协议。

## [1.1.6] - 2026-08-16

### Changed

- 录音期间 BLE 明确断开或音频连续中断时，本次 Typeless 听写会被标记为失败，不再静默续接残缺语音。
- 瞬断时保持 `M5 StopWatch Mic` 虚拟输入和系统默认输入不变，只输出静音并自动恢复 BLE 链路。
- Bridge 会结束当前 Typeless 听写并保留已有文字；重连后只恢复待机，用户按 A 或原 Typeless 快捷键即可开始一段新录音。
- 菜单栏持续显示“录音中断”，直到用户开始重试；空闲状态下的 BLE 重连仍保持静默。

### Fixed

- 修复按需麦克风在录音中途重连后停在 `Ready`、导致后续讲话全部丢失但 Typeless 仍显示录音的问题。

## [1.1.5] - 2026-08-16

### Changed

- 新固件连接后进入按需麦克风待机，空闲时虚拟输入保持可用并输出静音，不再持续消耗 BLE 音频带宽。
- 监控用户原有的 Typeless 主快捷键，使用 Mac 键盘启动听写时也会立即请求设备开始音频流。
- Typeless 进入 Processing 或 Idle 后请求设备停止音频；设备保留短尾段以避免切换瞬间截断。

### Compatibility

- 新 Bridge 连接旧固件时会自动回退到连续音频模式，不影响现有虚拟麦克风功能。

## [1.1.4] - 2026-08-16

### Changed

- 录音、处理和空闲状态继续通过轻量状态 characteristic 实时同步。
- 状态变化不再连带推送完整额度面板，减少设备端静态 UI 重绘；额度仍在连接成功和定时刷新时推送。

## [1.1.3] - 2026-08-16

### Fixed

- Typeless 模式下 A 键按下恢复为切换录音，松开不再立即停止。
- Processing 状态下再次按 A 会直接开始新录音，本地会话状态不再被 Typeless 的短暂 idle 检测覆盖。

## [1.1.2] - 2026-08-16

### Fixed

- 修复 BLE 音频包仍在持续传输、系统仍显示 `M5 StopWatch Mic`，但 Core Audio 虚拟输出引擎停转后输入变成全静音的问题。
- 健康检查现在分别监控 BLE 音频包、ADPCM 解码和 Core Audio 渲染回调；输出停转时只重建音频引擎，不重启整个 Bridge，也不重置原默认输入记录。
- 音频通知流停止时会自动重新订阅并重新发送 Start；设备流序号归零不再被误计为大量丢包。

### Verified

- 故障现场直接采样从 `Peak/RMS = -inf` 恢复到 Peak `-22.66 dB`、RMS `-39.29 dB`。
- 故障注入测试确认输出引擎从 `healthy=true` 到主动停止后的 `healthy=false`，再通过同一重建路径恢复为 `healthy=true`。
- 恢复后设备端和 Bridge 端丢包计数均为 0。

## [1.1.1] - 2026-08-16

### Fixed

- 修复 StopWatch 仍作为 BLE 键盘连接时，Bridge 的音频 GATT 断线后无法自动恢复的问题。
- 启动、连接失败、断线和健康检查现在都会优先接管 macOS 已连接的 `M5Codex-*` 设备，再回退到 BLE 扫描。

### Verified

- 在 `M5Codex-RO` 保持 HID 连接、Bridge 音频连接已经断开的现场状态下，重新建立 GATT 服务并恢复 16 kHz 音频包接收。

## [1.1.0] - 2026-08-16

### Added

- 新增菜单可选的 `M5 StopWatch Mic` 虚拟麦克风模式。
- 新增 BLE 16 kHz、20 ms、4-bit IMA-ADPCM 实时音频接收和解码。
- 新增独立 Core Audio HAL 输入驱动、产品 PKG 构建和对应 BlackHole GPLv3 源码归档。
- 新增包序号缺口静音补齐、设备端发送/丢包统计和默认输入恢复。

### Verified

- Typeless 实际语音识别成功；3,118 个连续音频包的实机验收区间丢包为 0。

## [1.0.3] - 2026-06-14

### Fixed

- 稳定 Accessibility 权限、设备事件处理和 Typeless 状态防抖。
- 新增本地稳定代码签名身份，减少更新 App 后重复授权。
- 简化 Bridge 输入链路：真实按键由固件 BLE HID 发送，App 只观察和同步状态。

## [1.0.2] - 2026-06-13

### Changed

- 与设备端摇晃清除灵敏度和语音输入工作流文档同步发布。
- 保持 Bridge 协议兼容，继续使用设备端 HID 输入路径。

## [1.0.1] - 2026-06-13

### Fixed

- 修复 Typeless 录音/处理中状态同步和 fallback 按键绑定。
- 稳定 Accessibility 探测、BLE 状态写入、按下/松开事件和处理完成防抖。
- 将输入按键责任明确移回设备固件 BLE HID。

## [1.0.0] - 2026-06-13

### Added

- 首次发布 StopWatch BLE Bridge 菜单栏 App。
- 支持 `M5Codex-*` 连接、Codex 额度推送、输入模式和按键配置同步。
- 支持 Typeless 状态观察、LaunchAgent 安装和 arm64 ZIP 发布包。

[1.3.2]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v1.3.2
[1.3.1]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v1.3.1
[1.3.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v1.3.0
[1.2.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v1.2.0
[1.1.8]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.1.8
[1.1.7]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.1.7
[1.1.6]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.1.6
[1.1.5]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.1.5
[1.1.4]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.1.4
[1.1.3]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.1.3
[1.1.2]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.1.2
[1.1.1]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.1.1
[1.1.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.1.0
[1.0.3]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.0.3
[1.0.2]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.0.2
[1.0.1]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.0.1
[1.0.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v1.0.0
