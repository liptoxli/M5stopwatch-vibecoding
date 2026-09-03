# Changelog

本项目采用[语义化版本](https://semver.org/lang/zh-CN/)；所有版本更新都在这里记录，并使用同版本 Git 标签发布。

此文件记录固件历史。Mac Bridge 独立更新：最新 **v1.4.1（2026-09-03）** 与固件 v0.10.7 增加麦克风就绪握手，避免开机或重连后的第一次听写无声。详见 [Bridge Changelog](tools/typeless_bridge/CHANGELOG.md)。

## [Unreleased]

## [0.10.7] - 2026-09-03

### Changed

- A/B 的开始语音操作改为先检查 Bridge 提供的麦克风就绪状态；未就绪时保留一次请求并显示 `MIC LINKING`。
- 就绪条件包括 BLE 音频通知、按需录音武装、虚拟输出健康和安全路由验证；Stats 通知只参与诊断，不阻塞可用音频。
- 新固件连接不包含就绪字段的旧 Bridge 时自动沿用旧启动行为，便于分步升级。

### Fixed

- 修复设备刚开机或 BLE 重连时，HID 已经可用但自定义音频链路仍在建立，导致 Typeless 被触发却收不到麦克风音频的问题。
- 3 秒内仍未就绪时取消本次启动并进入可重试的中断状态，不把坏会话当成正常录音。

### Verified

- ESP-IDF v5.5.4 全量构建、USB 应用分区刷写和 Flash 哈希校验通过，实机固件为 v0.10.7。
- Bridge v1.4.1 完成 Swift 构建和麦克风就绪、云同步、音频路由回归测试，并在 Mac mini 与 iMac 以各自稳定签名安装验证。

## [0.10.6] - 2026-08-25

### Changed

- 音频通知在 NimBLE 可用 mbuf 不足时短暂让出，并始终为 HID、状态和 CCCD 交互保留缓冲。
- 麦克风统计升级为 v4，增加最后通知错误、连续失败数和当前可用 mbuf 数。

### Fixed

- 修复长时间或连续语音输入时，高频 BLE 音频包可能耗尽 NimBLE 共享队列，导致按键仍可用但虚拟麦克风持续无声的问题。
- 每次新录音重置诊断计数，并限制重复错误日志频率，避免故障期间增加额外负载。

### Verified

- ESP-IDF v5.5.4 全量构建通过，应用分区刷写与 Flash 哈希校验成功，设备启动版本确认为 v0.10.6。
- Bridge v1.3.5 完成本机构建、安装和启动；设备回传 stats v4，空闲状态为 `notify_error=0` 且 `free_mbufs=36`。

## [0.10.5] - 2026-08-23

### Changed

- A 键开始语音时先锁存录音意图，再等待 BLE 连接、音频通知订阅和 Bridge `Armed` 状态全部就绪。
- BLE 麦克风 GATT 定义显式初始化所有字段，清理 ESP-IDF v5.5.4 下的相关编译警告。

### Fixed

- 修复 StopWatch 开机后原生 HID 已经可用，但 Mac Bridge 的自定义音频服务尚未就绪时，第一次 A 键录音请求被丢弃、麦克风不传流的问题。
- 如果音频通道 6 秒内仍无法就绪，固件会取消本次 Typeless 输入并进入录音异常提示，不再留下“看起来在录音、实际没有音频”的状态。

### Verified

- ESP-IDF v5.5.4 重新配置与全量构建通过，应用分区刷写和 Flash 哈希校验成功，串口确认实际启动版本为 v0.10.5。
- 受控重启时约 4.4 秒完成 BLE 音频通知订阅；实机验收确认开机后麦克风可自动接入并正常语音输入。

## [0.10.4] - 2026-08-22

### Changed

- Codex Vendor HID 使用实际连接句柄和 GATT 通知结果判断通道可用性，不再仅依赖可能滞后的本地订阅标志。
- RPC 请求和回复日志增加帧数、通知尝试和成功计数，宿主通讯状态只在回复成功后更新。
- 更新 ArduinoJson 文档用法，清理旧 `StaticJsonDocument` / `DynamicJsonDocument` 带来的构建警告。

### Fixed

- 修复 macOS 在缓存 BLE 重连后没有重放本地 SUBSCRIBE 事件时，设备明明可以完成 `device.status` 等 RPC，却仍显示 `Codex Micro Offline` 的问题。
- 修复上述误判造成的四个 Agent 触摸点被提前拦截、Codex 端无法收到操作的问题。

### Verified

- ESP-IDF v5.5.4 全量构建和 USB 刷写通过，启动版本确认为 v0.10.4。
- 实机日志确认 `v.oai.rgbcfg`、`v.oai.thstatus` 和 `device.status` 请求均成功回复；后续连接、按键和语音输入正常。

## [0.10.2] - 2026-08-22

### Changed

- 将 NimBLE CCCD 容量由 8 提升至 16；当前统一 HID、Bridge、麦克风、电池和 Service Changed 实际需要保存 10 条订阅。
- BLE 启动日志新增 bond 与 CCCD 数量，方便定位配对持久化和订阅容量问题。
- 重复配对事件不再自动删除设备端 bond；需要重置配对时改由用户明确执行，避免在回调中产生半删除状态。

### Fixed

- 修复 NimBLE 删除旧 peer 时先删除安全密钥、随后 CCCD NVS 删除失败，导致当前连接暂时可用但下次重启 `bonds=0` 的问题。
- 修复设备重启后 macOS 保留旧密钥、StopWatch 已丢失密钥，最终只能通过“忽略此设备”重新配对才能恢复的问题。

### Verified

- ESP-IDF v5.5.4 全量构建及 USB `app-flash` 成功，应用镜像 SHA-256 为 `6385b6782b810dd1b50599b4ce9a20fa6c54cfbbb426aacc70adf1d1080764e9`。
- 刷机启动及连续两次受控重启均保持 `bonds=1`、`cccds=10`、加密成功，并自动恢复原生 HID、Bridge event stream 和麦克风通知。

## [0.10.1] - 2026-08-21

### Changed

- 固件遇到 macOS 保留了旧配对信息时，只清理旧 bond 并重试一次，不再在同一连接中无限重复安全协商。
- macOS Bridge 先等待系统原生 HID 连接由 IOHID 确认，再连接 Companion 和音频服务，避免与系统配对流程竞争。

### Fixed

- 修复首次连接有时需要反复忽略设备、重新配对两三次才能完成的问题。
- 修复连接已经建立后 macOS 仍持续弹出设备认证提示的问题。
- 修复 BLE 重连或较长时间空闲后，虚拟麦克风仍显示可用但实际没有音频的问题。

### Verified

- ESP-IDF v5.5.4 构建、USB 应用刷写和 Flash 哈希校验通过。
- 实机重启后 macOS 原生 HID、Bridge event stream 和按需麦克风均自动恢复；Typeless 语音输入恢复正常。

## [0.10.0] - 2026-08-20

### Added

- 新增 Codex Micro 兼容的 Vendor BLE HID 通道，并与现有键盘、Consumer Control 合并到同一个 HOGP 服务。
- 新增四个原生 Agent 槽位入口，对应 `AG00` 至 `AG03`，可接收并显示宿主返回的颜色、亮度、灯效和速度。
- 新增顶部推理等级左右滑动，支持一次手势连续改变多级，并在界面上显示 `LOWER/HIGHER` 反馈。
- 新增中心四向 Radial 控制：长按进入后可输出上、下、左、右的连续角度与距离。
- 新增 `AGENTS.md` 和 `docs/AGENT_DEVELOPMENT_GUIDE.md`，为代码 Agent 与二次开发者提供模块、参数、协议和验收指南。

### Changed

- OpenWatcher V2 下沿由六个 Agent 点精简为四个，沿圆形边框排列，增大可触摸面积并减少误触。
- Agent 点改为两段式交互：触碰时先轻振和放大预览，保持 480ms 后再强振并提交。
- 顶部 `Codex Ready / Linking / Offline` 只表示原生通讯状态，不再承担未读数量显示。
- macOS Bridge 更新为 v1.3.0，可在 BLE 重连时恢复 HID Report 订阅，并同步推理等级确认状态。
- HID 设备描述更新为 Codex Micro 兼容布局；首次升级会执行一次配对结构迁移，macOS 端需要忽略旧设备并重新配对。

### Fixed

- 修复触摸长按回调把“槽位灯效尚未分配”误判成 Agent 不可用的问题；槽位动作现在始终交给底层 HID 在线检查。
- 修复多个 HOGP 服务在 macOS 上只暴露第一个服务，导致 Vendor Report 无法稳定被 Codex 发现的问题。
- 修复蓝牙超时重连后标准按键正常、但 Codex Vendor Report 未重新订阅的问题。

### Verified

- ESP-IDF v5.5.4 构建通过，v0.10.0 应用分区保留约 38% 空间。
- 实机完成 USB 应用刷写、Flash 哈希校验、重新配对、Bridge 自动恢复和虚拟麦克风健康检查。
- 四个 Agent 点、两段式触摸、Codex 端槽位切换、推理等级、中心触摸、A/B 键和实时语音输入均完成实机验收。

## [0.9.2] - 2026-08-18

### Changed

- 最近四小时活动方格恢复为每列上、下交替的时间顺序，让短时间使用也能同时利用两排显示；统计窗口、强度算法和历史数据保持不变。

### Verified

- ESP-IDF 重新配置后确认应用元数据为 v0.9.2，固件构建通过，应用分区仍有约 41% 空间。
- 同一坐标修改的 v0.9.1 实机构建已完成 USB 刷写、Flash 哈希校验、启动和 BLE Bridge 自动连接验收。

## [0.9.1] - 2026-08-18

### Changed

- Classic / Pet 与 OpenWatcher V2 共用同一套语音按键交互，不再因界面主题不同而产生行为差异。
- Typeless 处于录音、识别或异常恢复阶段时，B 键临时执行与 A 键相同的开始、停止或重试操作。
- 只有 Typeless 回到 Ready 后，B 键短按才发送 Return，长按才发送配置的确认动作，确保识别文字可以先人工检查。
- 重绘 GitHub 首页中的 Classic / Pet 预览，恢复真实 Pet、状态卡、连接圆点和连续额度弧线布局。

### Fixed

- 修复识别尚未结束时按 B 可能提前提交文字并造成设备与 Typeless 状态不同步的问题。
- 修复 B 键按下、长按或释放期间状态发生变化时可能落入错误确认分支的竞态。

### Verified

- ESP-IDF 构建通过，应用分区仍有约 41% 空间。
- USB 应用刷写、Flash 哈希校验、设备启动和 BLE Bridge 自动连接均通过。
- 实机验证 A 开始录音后可由 B 停止；回到 Ready 后 B 仍可正常确认发送。

## [0.9.0] - 2026-08-18

### Added

- OpenWatcher V2 标题新增 Codex 未读任务状态：无未读显示 `Codex Clear`，1/2/3+ 个未读分别使用黄、橙、红色提醒。
- macOS Bridge 从本机 Codex 状态读取未读任务，并过滤已经归档的本地任务；仅在数量变化时通过现有 BLE 写入队列同步。
- 新增滚动四小时活动热力图：24 个方格每格代表 10 分钟，按照真实录音时长和录音启动频率计算使用强度。
- 新增完整的真实续航测试记录，并在项目首页公开测试边界、实际开机时间和低电量显示限制。

### Changed

- 活动方格改为按时间顺序排列：上排是较早的两小时，下排是最近两小时，均从左向右推进。
- 活动强度改为六级蓝青色阶，空闲、轻度、中度和重度使用之间更容易区分。
- 空闲、处理中和额度同步不再放大活动强度；热力图只表达设备端实际语音使用和少量实体交互。
- 重写 GitHub 项目首页，统一展示项目现状、两套 UI、使用方式、续航、隐私和许可证。
- macOS Bridge 更新为 v1.2.0。

### Verified

- 固件完成 ESP-IDF 构建、USB 刷写和实机启动验收。
- BLE 加密连接、Bridge 配置、未读数量、额度面板和虚拟麦克风待命状态均通过日志确认。
- 实际使用中完成 Typeless 语音输入与长内容录入，未发现新的断线或按键问题。

## [0.8.1] - 2026-08-17

### Changed

- BLE 连接恢复为已长期验证的固定 15ms 间隔，不再在空闲和录音之间动态切换连接参数。
- 麦克风启动和 Codec 唤醒移出 BLE GATT 回调，改由独立音频任务异步执行，避免阻塞 ATT 响应。
- 首次录音后保持 Codec 热启动；空闲时关闭输入并释放 240MHz 性能锁，再次按 A 可以快速开始录音。
- macOS Bridge 更新为 v1.1.8：麦克风控制、状态和额度面板写入按优先级串行发送，面板分片等待每次写入确认后再继续。
- Bridge 主安装脚本同步安装 60 秒进程守护；异常退出后自动重新启动菜单栏 App。

### Fixed

- 修复 v0.8.0 中长时间 ATT 响应超时、录音流停顿并最终被 macOS 强制断开的问题。
- 修复额度面板分片、状态同步和麦克风控制并发写入时放大 BLE 拥塞的问题。

### Compatibility

- 保留两套 UI、实体按键、振动、提示音、BLE HID、Typeless 虚拟麦克风、省电降频、息屏和自动关机机制；不修改音频数据格式或虚拟麦克风驱动。

## [0.8.0] - 2026-08-16

### Changed

- 修正最终 ESP-IDF 配置，让 CPU 动态电源管理真正生效；正常界面保持 160MHz 基线，录音和 FFT 持有 240MHz 性能锁，变暗和息屏时回到 80MHz。
- 空闲时挂起 ES8311 Codec 和 I2S 数据通道；A 键开始录音时先同步唤醒音频并保留 20ms 稳定时间，停止后仍保留原有 400ms 尾段。
- BLE 在空闲连接时使用 30ms 间隔，录音时切回 15ms；不断开、不重新配对，并保留参数更新重试。
- 主循环降低固定唤醒频率，LVGL 改用自适应定时并在 activity sleep 停止 tick timer。
- 全局 IMU 读取增加 50ms 共享采样窗口，避免 Codex 页面和活动检测重复访问同一批数据。
- 电池审计新增 80/160/240MHz 频率驻留、音频输入占比和息屏占比；重新插电后延迟重复输出结果，方便手动接入串口查看。
- 补齐公开构建默认配置中的 Montserrat 48 字体，确保第二套 UI 从干净仓库直接编译。
- 第二套 UI 的 24 个活动方格从最近 2 小时调整为最近 4 小时，每格代表 10 分钟；左侧状态改为周额度刷新倒计时。

### Compatibility

- 保留两套 UI、A/B 实体按键、振动、提示音、摇一摇、BLE HID、Codex 额度、Typeless 虚拟麦克风和 15 分钟 PMIC 关机机制。
- macOS Bridge 更新为 v1.1.7，仅扩展活动统计窗口；未改动 BLE 音频协议或虚拟麦克风驱动。

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

[Unreleased]: https://github.com/liptoxli/M5stopwatch-vibecoding/compare/v0.10.7...HEAD
[0.10.7]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.10.7
[0.10.6]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.10.6
[0.10.5]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.10.5
[0.10.4]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.10.4
[0.10.2]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.10.2
[0.10.1]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.10.1
[0.10.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.10.0
[0.9.2]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.9.2
[0.9.1]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.9.1
[0.9.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.9.0
[0.8.1]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.8.1
[0.8.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.8.0
[0.7.4]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.4
[0.7.3]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.3
[0.7.2]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.2
[0.7.1]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.1
[0.7.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/releases/tag/v0.7.0
[0.6.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/tree/v0.6.0
[0.5.0]: https://github.com/liptoxli/M5stopwatch-vibecoding/commit/ac120b2
