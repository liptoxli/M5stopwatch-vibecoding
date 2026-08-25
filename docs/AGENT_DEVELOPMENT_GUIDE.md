# Agent 与二次开发指南

本文面向使用 Codex、Claude Code、Cursor 或其他代码 Agent 修改本项目的开发者，也适合第一次接触 ESP-IDF、LVGL、BLE HID 和 macOS Core Audio 的贡献者。

目标不是要求 Agent 一次读懂整个仓库，而是让它能快速回答四个问题：

1. 要改的功能属于哪个模块？
2. 哪些参数控制它？
3. 这项修改可能影响哪些现有链路？
4. 怎样证明改动没有破坏语音、按键、蓝牙或功耗？

## 1. 当前可依赖的产品基线

| 组件 | 版本 | 已验证能力 |
| --- | --- | --- |
| StopWatch 固件 | v0.10.6 | 两套 UI、A/B 键、摇晃、实时 BLE 麦克风、额度、四小时热力图、四 Agent、推理滑动、中心四向 Radial、持久化配对保护、Vendor HID 在线自校验、开机录音意图锁存与 BLE 音频背压 |
| macOS Bridge | v1.3.5 | BLE Companion、虚拟麦克风、Typeless 状态、额度与活动同步、原生 HID 稳定交接、快速重新发现、麦克风录音前自愈、断流逐级重连、Typeless 自动拉起、推理等级同步与回退 |
| 虚拟麦克风 | `M5 StopWatch Mic` | 16 kHz 单声道 PCM 输入，由 Bridge 写入本机 Core Audio 驱动 |

以下能力已经完成真实设备验收，可以作为回归基线：

- A 开始语音，A 或 B 在识别流程中停止；只有回到 Ready 后 B 才恢复确认/发送。
- StopWatch 麦克风以 IMA-ADPCM 实时传到 Mac，不生成 WAV。
- 四个 Agent 点完成触碰预览、480ms 长按确认和 Codex 端槽位切换。
- 顶部左右滑动可以改变推理等级。
- 中心长按后可发送四向 Radial 事件。
- BLE 重新配对后，Bridge、虚拟麦克风和 Codex Vendor HID 可以同时工作。
- Bridge 必须等待 macOS 原生 IOHID 设备出现后再连接自定义服务；不要移除这一配对时序保护。
- 不要在 `BLE_GAP_EVENT_REPEAT_PAIRING` 回调中自动删除 peer；NimBLE 会先删除安全密钥再删除 CCCD，后一步失败会留下下次启动无法恢复的半删除 bond。
- 统一 HID、Bridge、麦克风、电池和 Service Changed 当前会持久化 10 条 CCCD，`CONFIG_BT_NIMBLE_MAX_CCCDS` 不得低于 16。
- 新录音开始前会预检虚拟输入路由，空闲超过 8 秒或输出不健康时重建音频引擎。
- Typeless 未运行时，Bridge 会在后台启动它，等待快捷键注册后补发当前这一次 A 键；修改这条链路时必须保留单次补发和超时保护。

未读 task 的解析、同步和列表 UI 已有代码基础，但当前默认主界面没有绑定“打开任务列表”的入口。除非需求明确要求，不要把它写成已经交付的默认操作，也不要擅自占用中心点或四个 Agent 点。

## 2. 先理解三条独立数据链路

```text
实体键 / 触摸
    ├── 标准 BLE HID ────────────────> macOS 快捷键 / Typeless / 输入法
    ├── Codex Vendor HID ────────────> Codex Agent / Encoder / Radial
    └── Companion BLE event ─────────> StopWatch BLE Bridge 的辅助动作

StopWatch 麦克风
    └── BLE IMA-ADPCM ─> Bridge 解码 ─> M5 StopWatch Mic ─> Typeless

Mac 本机 Codex 状态
    └── Bridge ─> BLE 分片面板 ──────> 额度、推理标签、活动热力图和状态
```

不要把三条链路混为一条：

- Bridge 退出后，已经配对的标准键盘 HID 仍应工作。
- 关闭虚拟麦克风后，Agent 和推理控制仍应工作。
- Codex Vendor HID 暂时未订阅时，语音链路仍可能完全健康。

诊断时必须分别检查“系统 HID”“Bridge Companion”和“麦克风 Audio”状态。

## 3. 仓库模块地图

### 3.1 固件入口

| 模块 | 主要文件 | 作用 |
| --- | --- | --- |
| Codex App 控制器 | `firmware-stopwatch-idf/main/apps/app_codex/app_codex.cpp` | 汇总按键、触摸请求、BLE 状态、额度和录音状态 |
| Codex UI | `firmware-stopwatch-idf/main/apps/app_codex/view/view.cpp`、`view.h` | 两套 UI、热力图、波形、四 Agent、推理面板、中心四向控制 |
| Codex 配置 | `firmware-stopwatch-idf/main/apps/app_codex/codex_config.h` | 示例 URL、刷新周期、Wi-Fi 占位符和输入默认值 |
| 面板解析 | `firmware-stopwatch-idf/main/apps/app_codex/codex_quota_client.cpp` | 接收 Bridge/HTTP 面板，解析额度、活动和推理标签 |
| BLE Companion | `firmware-stopwatch-idf/main/hal/ble_bridge.cpp`、`ble_bridge.h` | 配对、标准按键、Bridge 状态、面板分片和 task 摘要 |
| Codex Micro HID | `firmware-stopwatch-idf/main/hal/codex_micro_hid.cpp`、`.h` | 统一 HID Report Map、Vendor RPC、Agent、Encoder、Radial、宿主灯效状态 |
| BLE 麦克风 | `firmware-stopwatch-idf/main/hal/ble_microphone.cpp`、`.h` | 音频任务、20ms 分帧、通知、按需启停和统计 |
| ADPCM | `firmware-stopwatch-idf/main/hal/ima_adpcm.cpp`、`.h` | PCM 与 4-bit IMA-ADPCM 编解码 |
| 电源与硬件 | `firmware-stopwatch-idf/main/hal/hal_*.cpp` | CPU、PMIC、显示、音频、按键、IMU、RTC |
| 版本显示 | `firmware-stopwatch-idf/version.txt`、`main/apps/common/common.h` | ESP-IDF 元数据和设备 About/启动页版本 |

### 3.2 macOS 入口

| 模块 | 主要文件 | 作用 |
| --- | --- | --- |
| Bridge 主程序 | `tools/typeless_bridge/stopwatch_ble_bridge.swift` | 菜单栏、CoreBluetooth、Codex 状态、Typeless、音频管线和重连 |
| Bridge 构建 | `tools/typeless_bridge/build_stopwatch_ble_bridge.sh` | 构建 `.app` |
| 本机安装 | `tools/typeless_bridge/install_launch_agent.sh` | 安装 App 与 LaunchAgent |
| 发布打包 | `package_release.sh`、`package_product_installer.sh` | ZIP/PKG 发布产物 |
| 虚拟麦克风 | `tools/typeless_bridge/virtual_mic_driver/` | `M5 StopWatch Mic` Core Audio HAL 驱动 |
| Bridge 版本 | `tools/typeless_bridge/VERSION` | Bridge 单一版本源 |

## 4. 用户交互与真实参数

### 4.1 实体按键

默认设置：

| 操作 | 默认行为 | 备注 |
| --- | --- | --- |
| A 短按 | `F19`，开始/停止语音 | 由固件直接发送 BLE HID |
| B 短按，语音流程中 | 临时代替 A | 防止识别未完成时误发送 |
| B 短按，Ready | `Return` | 确认或发送 |
| B 长按 | `Command+Return` | 默认长按时间为 2 秒，可在 Bridge 配置 |
| 摇晃 | Clear Input | 默认通过已配置的清除动作 |
| A+B | 退出 Codex App | 回到设备 Launcher |

Bridge 可选键位：`F13`–`F20`、`Return`、`Space`、`Tab`、`Escape`。修改输入逻辑时必须同时验证 Classic / Pet 和 OpenWatcher V2，因为两套 UI 共用同一交互状态机。

### 4.2 四个 Agent 点

参数位于 `view.cpp`：

| 参数 | 当前值 | 含义 |
| --- | --- | --- |
| `kNativeAgentCenters` | `(99,367)`、`(184,417)`、`(282,417)`、`(367,367)` | 四个可见点中心，适配 466×466 圆屏 |
| `kNativeAgentHitSize` | `84 px` | 每个点的透明方形触摸区 |
| `kNativeAgentHoldMs` | `480 ms` | 从预览到真正提交的时间 |
| `kNativeAgentPreviewFrameMs` | `80 ms` | 长按进度视觉刷新周期 |
| `kNativeAgentSwitchHysteresis` | `8 px` | 手指微抖时保留当前候选点 |

交互顺序：

1. 手指按下，轻振 `14 ms / 35%`，圆点开始放大。
2. 在 480ms 内松手，不发送任何 Agent 事件。
3. 保持到阈值，强振 `35 ms / 75%`，向传输层排队槽位。
4. `tap_agent_slot()` 发送一次按下和一次释放，间隔 24ms。

槽位灯效是否 `assigned` 只控制显示，不是是否允许发送的条件。在线判断只能由 `codex_micro_hid::ready()` 和最终的 `tap_agent_slot()` 完成，否则重连后尚未收到灯效时会出现错误的 `AGENT NOT READY`。

### 4.3 顶部推理等级

| 参数 | 当前值 |
| --- | --- |
| 触摸范围 X | `120..346` |
| 触摸范围 Y | `28..112` |
| 每级滑动距离 | `44 px` |
| 单次手势上限 | `-6..+6` 级 |

触摸顶部时立即显示推理面板并轻振。向左发送降低，向右发送提高；每跨过一个 44px 阶梯产生一次新增量和一次触觉反馈。

固件优先发送 Codex Vendor HID Encoder：

- 提高：`ENC_CC`
- 降低：`ENC_CW`
- 动作：`act=2`
- 单次请求最多 8 步

Bridge 收到 `codex_reasoning:native_sync` 后延迟刷新确认标签。只有原生通道不可用时，Bridge 才串行调用 Codex 命令面板回退；不要把回退改成并发输入。

### 4.4 中心四向 Radial

| 参数 | 当前值 | 含义 |
| --- | --- | --- |
| `kActionWheelStartRadius` | `72 px` | 中心可开始长按的半径 |
| `kActionWheelHoldMs` | `480 ms` | 进入四向模式的长按时间 |
| `kNativeControlHoldSlop` | `22 px` | 长按确认前允许的移动 |
| `kNativeControlDeadZone` | `24 px` | 进入方向输出的死区 |
| `kNativeControlFullScale` | `120 px` | Radial 距离达到 1.0 的参考位移 |
| `kNativeRadialFrameMs` | `50 ms` | 连续事件最大约 20Hz |

方向映射：

| 方向 | angle |
| --- | --- |
| 右 | `0.00` |
| 下 | `0.25` |
| 左 | `0.50` |
| 上 | `0.75` |

距离在越过死区后从 `0.15` 线性增加到 `1.0`。松手、切换页面或开始录音时必须发送 `distance=0` 归中，避免 Codex 端保留粘住的方向状态。左右目前保留给用户在 Codex 内自行映射，不应擅自改成鼠标或系统键盘。

### 4.5 语音和虚拟麦克风

| 参数 | 当前值 |
| --- | --- |
| 采样率 | `16,000 Hz` |
| 声道 | 单声道 |
| PCM | 16-bit signed |
| 帧长 | `20 ms` / 320 samples |
| 压缩 | 4-bit IMA-ADPCM |
| 每包 | 14-byte header + 160-byte ADPCM |
| 稳态 BLE 速率 | 约 `8.7 KB/s` |
| 解码 PCM 速率 | `32 KB/s` |

空闲时采用按需待机，不持续发送音频。开始语音前唤醒 Codec 和采集任务，停止时保留短尾段。Bridge 对丢帧填静音，但如果录音中发生明确断线或持续断流，会终止本次会话并提示用户重新录制，不尝试把缺失中段静默拼接起来。

v0.10.5 起，A 键开始语音时先锁存用户意图。如果 macOS 的自定义音频服务尚未完成订阅，固件会在 `Armed + connected + subscribed` 都成立后自动开流，而不是丢弃第一次 A 键。等待超过 6 秒则取消本次 Typeless 输入并进入既有的录音异常提示，防止界面显示录音但没有音频。

v0.10.6 起，音频任务在 NimBLE 共享 mbuf 紧张时最多等待 80ms，并保留至少 4 个 mbuf 给 HID、状态和 CCCD 流量。stats v4 在原有计数后增加 `last notify error`、`consecutive failures` 和 `free mbufs`。Bridge 在持续断流时先重建 Audio 订阅，60 秒内再次出错则重建 BLE 连接；仍会终止当前 Typeless 听写并让用户重说，不会自动拼接丢失的语音中段。

### 4.6 活动热力图与额度

- 周额度只显示 weekly window，不恢复已经移除的 5h window。
- 当天用量以本地 08:00 为日边界。
- 活动窗口为最近四小时，共 24 格，每格 10 分钟。
- v0.9.2 起时间顺序为每列上、下交替，再进入下一列。
- Bridge 以 70% 实际录音时长和 30% 启动频率计算强度。
- 顶部渐变表达额度健康度；中央大数字只显示剩余量，左侧显示当天已用量。

## 5. Codex Micro 兼容层

### 5.1 BLE 身份与统一 HID

`codex_micro_hid.cpp` 将三个 collection 放进同一个 HOGP 服务：

1. Keyboard，Report ID `1`。
2. Consumer Control，Report ID `2`。
3. Vendor Defined，Usage Page `0xFF00`，Report ID `6`，Input/Output 均为 63 bytes。

Vendor RPC 使用 63-byte Report body：第 1 byte 是 channel，第 2 byte 是 fragment length，后面最多 61 bytes 数据。当前 RPC channel 为 `2`，最大拼装 JSON 为 4096 bytes。

固件响应的宿主方法包括：

- `sys.version`
- `device.status`
- `v.oai.thstatus`
- `v.oai.rgbcfg`
- `lights.preview`
- `host.focused_app`

固件主动发送：

- Agent：`v.oai.hid` + `AG00..AG03`
- Encoder：`v.oai.hid` + `ENC_CC/ENC_CW`
- Radial：`v.oai.rad` + `angle/distance`

这是针对当前 Codex Desktop/Codex Micro 行为完成实机验证的兼容实现，不应描述为 OpenAI 公布的稳定协议。修改 Report Map、VID/PID、Report ID 或 service layout 都可能要求 macOS 重新配对。

### 5.2 Agent 状态颜色

宿主通过 `v.oai.thstatus` 返回槽位状态。固件保存 `color`、`brightness`、`effect` 和 `speed`；亮度大于 0 且 effect 非 off 时视为已分配。常见语义色：

| 状态 | RGB |
| --- | --- |
| Working | `0x304FFE` |
| Unread | `0x00FF4C` |
| Attention | `0xFF6D00` |
| Error | `0xFF0033` |

UI 支持 solid、breath、shallow breath 等宿主效果。Agent 点的触摸发送与灯效分配必须保持解耦。

## 6. 常见改动应该改哪里

| 想改的内容 | 首选位置 | 不应顺手改动 |
| --- | --- | --- |
| Agent 点位置/大小/长按时间 | `view.cpp` 的 `kNativeAgent*` | HID Report Map、音频任务 |
| 推理滑动范围/灵敏度 | `kReasoningTouch*` | Bridge 音频或按键映射 |
| 中心手势死区/方向 | `kActionWheel*`、`kNativeControl*` | 标准键盘 HID |
| Agent 协议 | `codex_micro_hid.cpp` | Companion characteristic UUID |
| A/B 语音交互 | `app_codex.cpp` + Bridge 状态机 | 两套 UI 分别写不同逻辑 |
| 波形刷新或 UI 功耗 | `CodexView::frameIntervalMs()` 和差分刷新 | BLE connection interval |
| 音频格式/帧长 | `ble_microphone.cpp`、`ima_adpcm.*`、Bridge 解码 | 只改一端 |
| Bridge 菜单与状态同步 | `stopwatch_ble_bridge.swift` | 固件 NVS 配置格式，除非同时迁移 |
| 额度/活动算法 | Bridge panel builder + `codex_quota_client.cpp` | UI 里重复计算第二份真相 |

## 7. Agent 开发工作流

### 7.1 开始前

让 Agent 先输出：

- 目标模块和入口文件。
- 现有功能中必须保持不变的链路。
- 准备修改的参数及其当前值。
- 至少一个快速反馈信号，例如编译、日志或实机动作。

先运行：

```bash
git status --short
rg -n "目标符号或参数" firmware-stopwatch-idf tools
```

工作树可能包含用户尚未提交的改动。不要使用 `git reset --hard`、`git checkout --`、`git add .` 或批量覆盖与任务无关的文件。

### 7.2 构建固件

需要 ESP-IDF v5.5.4：

```bash
cd firmware-stopwatch-idf
python3 ./fetch_repos.py
idf.py build
```

首次安装、分区表变化或 HID 配对结构迁移时：

```bash
idf.py -p /dev/cu.usbmodemXXXX flash
```

确认 Bootloader 和分区布局兼容后，只更新应用：

```bash
idf.py -p /dev/cu.usbmodemXXXX app-flash
```

不要猜串口，先列出 `/dev/cu.usbmodem*`。刷写后必须看到 Flash hash verified 和 hard reset 成功。

### 7.3 构建 Bridge

```bash
tools/typeless_bridge/build_stopwatch_ble_bridge.sh
```

本机安装：

```bash
tools/typeless_bridge/install_launch_agent.sh
```

日志：

```bash
tail -n 120 ~/Library/Logs/stopwatch-ble-bridge.log
```

验收关注：

- `Connected. Discovering bridge and microphone services`
- `Device event stream subscribed`
- HID Report recovery subscription ready
- `microphone health ... output=healthy`
- `dropped=0` 或可解释的短时丢包

### 7.4 最小回归矩阵

| 变更类型 | 必测项 |
| --- | --- |
| UI | 两套主题、圆屏边缘裁切、状态变化、息屏恢复 |
| 触摸 | 短触取消、长按一次提交、滑动换目标、录音时不误触 |
| HID | A/B、四 Agent、推理、Radial、重连后再次触发 |
| 麦克风 | 开始、停止、长语音、虚拟输入电平、丢包统计 |
| Bridge | 登录启动、退出行为、自动重连、菜单配置保留 |
| 电源 | 空闲温度、屏幕变暗、息屏、录音后 CPU/Codec 回落 |

## 8. 版本与公开发布

固件版本必须同步：

- 根目录 `VERSION`
- `firmware-stopwatch-idf/version.txt`
- `firmware-stopwatch-idf/main/apps/common/common.h`
- `README.md`
- `CHANGELOG.md`

Bridge 版本必须同步：

- `tools/typeless_bridge/VERSION`
- `tools/typeless_bridge/README.md`
- `tools/typeless_bridge/CHANGELOG.md`
- 根目录 README 的兼容表

发布前运行：

```bash
tools/check_version.sh
cd firmware-stopwatch-idf
python3 ./fetch_repos.py
idf.py build
```

不要把以下内容提交到公开仓库：

- 真实 Wi-Fi SSID 或密码。
- 本机绝对路径。
- Codex access token、account ID 或 `auth.json` 内容。
- Apple Developer 签名身份和证书。
- 未经许可的第三方二进制或素材。

公开 `codex_config.h` 必须保留 `example.com` 和 `YOUR_WIFI_*` 占位符。Bridge 可以读取当前用户本机的 `~/.codex/auth.json`，但不得记录、复制或打包其中的凭据。

## 9. 推荐交给 Agent 的任务模板

```text
请先阅读 AGENTS.md 和 docs/AGENT_DEVELOPMENT_GUIDE.md。

目标：<只写一个明确功能>
必须保留：实时麦克风、A/B 语音状态机、四 Agent 原生 HID、两套 UI 一致性。
允许修改：<明确文件或模块>
不要修改：<协议、分区、配对或其他边界>

开始前请列出现有参数、影响范围和验证方法。
完成后运行版本检查与相关构建，并报告实机还需要验证的动作。
不要自动提交或推送，除非我明确要求。
```

这个模板能帮助 Agent 把产品意图、修改范围和验收标准绑在一起，减少“修了 UI 却破坏麦克风”或“优化省电却让按键变慢”一类跨模块回归。
