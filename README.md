# M5 StopWatch for Vibe Coding

<p align="center">
  <strong>把 M5Stack StopWatch 变成桌面上的 Codex 状态屏、语音输入控制器和实时蓝牙麦克风。</strong>
</p>

<p align="center">
  <img alt="Firmware v0.10.6" src="https://img.shields.io/badge/firmware-v0.10.6-6f5cff">
  <img alt="Bridge v1.4.0" src="https://img.shields.io/badge/macOS%20Bridge-v1.4.0-35b8ff">
  <img alt="ESP32-S3" src="https://img.shields.io/badge/hardware-ESP32--S3-ef6c35">
  <img alt="License MIT" src="https://img.shields.io/badge/license-MIT-55f36a">
</p>

<p align="center">
  <img alt="OpenWatcher V2 界面" src="docs/assets/openwatcher-v2-ui.svg" width="520">
</p>

M5 StopWatch for Vibe Coding 为 Codex、ChatGPT、Claude Code 和 IDE 工作流增加了一层独立的物理交互：按下实体键开始讲话，StopWatch 麦克风通过 BLE 实时传到 Mac，再由系统输入设备 `M5 StopWatch Mic` 交给 Typeless 等语音输入应用。屏幕同时显示 Codex 周额度、当天用量、最近四小时的使用强度、Agent 工作/未读/异常状态和连接状态。

它不是录音机，也不会先生成 WAV 文件。语音以 16 kHz IMA-ADPCM 实时流传输，停止讲话后即可进入识别和发送流程。

## 项目现状

当前版本为 **固件 v0.10.6 / macOS Bridge v1.4.0**。语音输入、虚拟麦克风、BLE HID、原生 Codex Micro 兼容交互和圆屏触摸已有实机使用基础。本次增加可选的跨电脑统计同步：在一台 Mac 上使用后，换到另一台 Mac，剩余额度、今日消耗和最近四小时的使用强度可以接着看。

同步服务由用户自行部署或接入已有服务器，没有绑定某台服务器，也没有内置公共账号。不开启云同步，仍可本地使用。**这次只需更新 Mac Bridge；使用跨电脑同步时再配置服务，无需重刷固件或重新配对。** 上一版固定虚拟输出、异常静音与重录提示，以及 BLE 音频缓冲保护、逐级重连和 A/B 交互继续保留。

本版已验证两台 Mac 能读取同一份统计，云端去重、离线缓存和音频路由回归测试通过。真实跨日、睡眠唤醒与长时间断网仍需持续观察。虚拟设备请保留 **48 kHz** 设置（蓝牙源音频仍是 16 kHz）；手动切换采样率的已知限制见 [Bridge 更新记录](tools/typeless_bridge/CHANGELOG.md#136---2026-08-31)。

| 能力 | 当前状态 |
| --- | --- |
| StopWatch 麦克风 → BLE → Mac 虚拟麦克风 → Typeless | 已完成，支持实时语音输入 |
| A/B 实体键、长按、摇晃操作 | 已完成，按键由设备通过 BLE HID 直接发送 |
| Codex Micro 原生 BLE HID 兼容层 | 已完成，Agent、推理等级和四向 Radial 事件走原生 Vendor HID 通道 |
| 下沿四个 Agent 触摸点 | 已完成，支持两段式长按确认、状态颜色、动画和防误触 |
| 顶部推理等级滑动 | 已完成，向左降低、向右提高，优先走原生通道并提供 Bridge 恢复路径 |
| 中心四向触摸控制 | 已完成，长按进入，随后可向上、下、左、右输出连续 Radial 事件 |
| Codex 周额度、当天消耗和重置倒计时 | 已完成，支持本地使用和可选的跨 Mac 统一统计 |
| Codex 状态反馈 | 已完成，顶部只表示通讯状态，四个 Agent 点显示槽位颜色、亮度和动态效果 |
| 最近四小时使用强度 | 已完成，24 格滚动窗口，每格 10 分钟；可合并多台 Mac 的设备活动 |
| Classic / Pet 与 OpenWatcher V2 两套 UI | 已完成，可在设备中切换 |
| 断线与音频异常处理 | 已完成，异常时结束本次听写，提示重录并自动重建音频链路 |
| 省电、息屏和自动关机 | 已完成，日常混合使用约为 5 小时级 |

当前版本适合实机体验、二次开发和日常语音输入。电量百分比仍以电池电压估算，低电量区会偏保守；因此页面中的续航数字是实测范围，不是实验室标称值。首次从 v0.9.x 升级到 v0.10.x 时，由于 HID 描述符发生变化，需要在 macOS 中忽略旧的 `M5Codex-*` 并重新配对一次；已经在 v0.10.x 正常配对的设备升级 v0.10.6 时无需再次忽略设备。

## v1.4.0：换一台电脑，统计也能接着看

- **剩余量统一**：读取同一 Codex 账号的官方周额度，不是各台电脑独立扣减。其他入口的使用若计入同一额度池，也会体现在下次读取的剩余量里。
- **Today 统一**：以每天北京时间 08:00 为起点，显示消耗了多少个周额度百分点；不是一份额外的每日额度。
- **四小时热力图统一**：合并各 Mac 记录到的录音时段和设备操作，保持 24 格、上下交替的显示方式。它不代表所有 Codex 任务或手机上的工作频率。
- **断网不假装归零**：保留缓存与待同步额度；缺失日基线显示不完整，首次旧记录迁移显示 `Today estimate`。
- **语音仍走原来的路**：不上传音频、识别文字、聊天内容或 OpenAI 登录凭据；不改变 A/B、触摸、Agent 或蓝牙音频协议。

云交换约每 60 秒一次，官方额度默认每 300 秒采集，表盘沿用原刷新间隔，录音中延后发送。至少需要一台已登录、开启额度同步的 Mac 正在运行；所有电脑休眠后，服务器不会自行获取新的官方额度。

已提供 [跨设备同步说明](docs/stopwatch-cloud-sync.md)、[可运行的服务端示例](server/cloud-sync/README.md) 和回归测试，方便大家接到自己的服务，或让其他设备读取同一份数据。地址、凭据和部署位置都由用户自己配置。

## 两套界面

<table>
  <tr>
    <th width="50%">Classic / Pet</th>
    <th width="50%">OpenWatcher V2</th>
  </tr>
  <tr>
    <td align="center"><img alt="Classic Pet UI" src="docs/assets/classic-pet-ui.svg" width="360"></td>
    <td align="center"><img alt="OpenWatcher V2 UI" src="docs/assets/openwatcher-v2-ui.svg" width="360"></td>
  </tr>
  <tr>
    <td>延续项目最早的桌面伙伴方向，以时间、额度弧线、Pet 动画和轻量状态反馈为核心。适合喜欢角色感和动态反馈的用户。</td>
    <td>当前重点优化的效率界面。信息层级更直接，强调剩余额度、当天消耗、四小时活动热力图和未读任务状态。</td>
  </tr>
</table>

### OpenWatcher V2 的设计重点

OpenWatcher V2 的界面方向参考了 [OpenWatcher](https://github.com/openwatcher-ai/openwatcher) 的 UI 设计思路，并在此基础上针对 `466 x 466` 圆形 AMOLED、Codex 周额度、最近四小时活动和实时语音输入场景重新设计。

- 顶部半圆进度条使用具有语义的渐变色：剩余额度越充足越接近绿色，额度紧张时逐步转为黄色、橙色和红色。
- 中央只突出“剩余百分比”，左侧单独显示当天已使用量，避免“已用”和“剩余”同时占据视觉中心。
- 24 个方格覆盖最近四小时，每格代表 10 分钟；颜色深浅由实际录音时长和启动频率共同决定。
- 顶部 `Codex Ready / Linking / Offline` 只表达设备与 Codex 原生通道是否正常通讯，不再混入任务数量。
- 下沿四个 Agent 点对应四个 Codex Agent 槽位；颜色、亮度和呼吸效果由 Mac 端原生状态返回，点位同时也是触摸入口。
- 静态区域按变化更新，录音波形限制刷新频率，兼顾实体按键响应、界面流畅度和功耗。

## v0.10.0：从状态屏到 Codex 物理控制器

本版本把 StopWatch 已有的 BLE 键盘、麦克风和圆形触摸屏整合为一套 Codex Micro 兼容交互，同时保留原来的语音输入与两套 UI。

### 四个 Agent 快捷入口

- 四个点沿屏幕下半圆排列，对应原生槽位 `AG00` 至 `AG03`。
- 可见圆点保持克制，但每个点背后都有 `84 × 84 px` 的透明触摸区。
- 手指落下时先轻振并放大预览；保持约 `480 ms` 后再次强振并真正切换。
- 提前松手不会发送；按住时滑向相邻点位会重新锁定目标并重新计时。
- 槽位是否已经返回灯效只影响显示，不再错误阻止按键发送；真正的在线状态由 HID 传输层统一判断。

### 推理等级和中心四向控制

- 触摸顶部区域并左右滑动：每移动约 `44 px` 改变一级推理强度，单次手势最多六级。
- 中心半径 `72 px` 内长按约 `480 ms` 后进入四向控制，移动超过 `24 px` 开始输出 Radial 距离。
- 四向角度与 Codex Micro 协议一致：右 `0.00`、下 `0.25`、左 `0.50`、上 `0.75`；松手发送距离 `0` 作为归中。
- 原生 Vendor HID 是主路径；Bridge 会在蓝牙重连时辅助恢复 Report 订阅，并在原生推理通道暂时不可用时提供受控的本机回退。

### 一条 BLE 连接承载三类能力

固件使用一个统一 HOGP 服务同时提供标准键盘 Report、Consumer Report 和 Codex Vendor Report，避免 macOS 只暴露第一个 HID 服务的问题。语音仍由同一 BLE 外设上的自定义麦克风服务传输，状态、额度和 Bridge 控制则使用原有 Companion 服务。三个通道相互独立：Bridge 退出时标准按键仍可用；麦克风关闭时 Codex Agent 控制仍可用。

## 为什么适合语音驱动的 Coding 工作流

- **不用切窗口**：盲按实体键即可开始或结束语音输入，思路不需要停下来找快捷键。
- **状态在桌面上可见**：录音、处理中、断线、额度和未读任务都由圆屏即时反馈。
- **是真正的系统麦克风**：Mac 端看到的是 `M5 StopWatch Mic` 输入设备，Typeless 等应用可以直接使用。
- **链路简单可控**：设备负责真实 BLE HID 按键，Mac Bridge 负责音频、状态和额度同步。
- **异常不会静默拼接**：录音中断后会结束本次听写并提示重说，避免把缺失中段的语音继续发送。

## 续航

当前省电策略包括 CPU 动态降频、麦克风按需启动、差分刷新、1 分钟降亮度、3 分钟息屏以及无外接电源时的 15 分钟自动关机。

2026-08-17 至 2026-08-18 的一次真实混合使用测试中：

- 累计实际开机时间为 **4 小时 33 分 55 秒**，关机时段已剔除。
- 从首次可靠的 86% 电量记录到插入 USB 为 **4 小时 20 分 51 秒**。
- 最后阶段包含屏幕常亮和频繁语音输入，属于偏重度使用。
- 按相同负载粗略估算，完整电量约为 **5 小时级**。
- 表显 0% 后设备仍继续运行至少 2 分 56 秒，说明低电量百分比仍需进一步校准。

详细测试边界和每段记录见 [续航测试记录](docs/BATTERY_TEST.md)，省电机制见 [省电与降温策略](docs/POWER_SAVING.md)。

## 使用方式

1. 将固件刷入 M5Stack StopWatch。
2. 在 macOS 蓝牙设置中配对名称为 `M5Codex-*` 的设备。
3. 安装并启动 StopWatch BLE Bridge，允许辅助功能权限。
4. 在菜单栏打开 `M5 StopWatch Mic`，并在语音输入应用中选择这个麦克风。
5. 使用 A 键控制语音输入。Typeless 没有运行时，Bridge 会自动启动并补发这次快捷键；Typeless 正在录音或识别时，B 键与 A 键执行相同交互，不会提前发送；回到 Ready 后，B 键才确认或发送。具体键位可在 Bridge 中配置。

Bridge 支持 Typeless 和微信输入法两种输入路径，也可以配合任何接受普通键盘快捷键和系统麦克风的应用使用。

## 功能概览

- 16 kHz、20 ms 分帧、4-bit IMA-ADPCM 的实时 BLE 音频。
- macOS 菜单栏 Bridge 与 `M5 StopWatch Mic` Core Audio 输入设备。
- BLE HID 实体按键、长按确认和摇晃清除。
- 兼容 Codex Micro 的统一 BLE HID：四个 Agent 槽位、推理等级 Encoder 和四向 Radial 输入。
- 圆屏两段式触摸确认：第一次触碰反馈、持续按住后提交，降低小屏误触。
- Codex 周额度、当天使用量、重置倒计时和四个原生 Agent 槽位状态。
- 最近四小时的设备使用强度热力图。
- Typeless 录音、处理、空闲和异常状态反馈。
- BLE 自动重连、音频输出自恢复和明确的录音中断提示。
- Wi-Fi 默认关闭；Codex 数据优先通过低功耗 BLE 同步。

完整说明可继续阅读：

- [功能说明](docs/FEATURES.md)
- [BLE 麦克风协议](docs/stopwatch-ble-microphone.md)
- [Codex 额度机制](docs/QUOTA.md)
- [跨设备统计同步与 API](docs/stopwatch-cloud-sync.md)
- [隐私与安全](docs/SECURITY_AND_PRIVACY.md)
- [Pet 自定义](docs/PET_CUSTOMIZATION.md)
- [Agent 与二次开发指南](docs/AGENT_DEVELOPMENT_GUIDE.md)

## 为 Agent 和二次开发准备的说明书

为了方便大家使用 Codex 或其他代码 Agent 进行二次开发，本仓库已经提供专门说明：

- 根目录 [AGENTS.md](AGENTS.md)：让支持仓库指令的代码 Agent 进入项目后，立即知道构建命令、代码边界、隐私要求和验收规则。
- [Agent 与二次开发指南](docs/AGENT_DEVELOPMENT_GUIDE.md)：逐模块说明功能、入口文件、数据流、交互参数、BLE 协议、修改方法、刷机步骤和回归检查。
- [跨设备同步说明](docs/stopwatch-cloud-sync.md)：统一统计口径、API 字段、配置、离线策略和服务端扩展方法。

推荐把下面这句话直接交给你的代码 Agent：

> 请先阅读 `AGENTS.md` 和 `docs/AGENT_DEVELOPMENT_GUIDE.md`，说明你准备修改的模块、不会影响的链路和验收方法，再开始改动。

指南按“一个功能、一组入口文件、一套验证方法”组织。你可以只改 UI、触摸阈值、按键映射、Agent 数量、麦克风参数或 Bridge 行为，而不需要先理解整个仓库。

## 版本历史

| 日期 | 固件 | Mac Bridge | 主要更新 |
| --- | --- | --- | --- |
| 2026-09-01 | v0.10.6（不变） | v1.4.0 | 可选跨 Mac 同步剩余额度、Today 和四小时热力图；统一 08:00 日界线、离线缓存、去重与自托管接入文档 |
| 2026-08-31 | v0.10.6（不变） | v1.3.6 | 修复麦克风声音误从 Mac 音响播放；固定虚拟输出、录音前核验路由、异常静音并提示重录；补充回归测试与已知限制 |
| 2026-08-25 | v0.10.6 | v1.3.5 | 为高频音频包增加 BLE 缓冲背压；增加可读断流诊断，Bridge 可逐级重建音频订阅和 BLE 连接 |
| 2026-08-23 | v0.10.5 | v1.3.4 | 开机后首次 A 键可等待音频通道就绪并自动开流；Bridge 更快接入 Companion 与虚拟麦克风 |
| 2026-08-22 | v0.10.4 | v1.3.3 | 修复 Agent 在线误判；A 键可自动拉起 Typeless，并在录音前恢复虚拟麦克风输出与 BLE 音频订阅 |
| 2026-08-22 | v0.10.2 | v1.3.2 | 永久修复重启后 bond 丢失；扩大 BLE 订阅容量，并让 Bridge 等待原生 HID 稳定后再接入音频与状态服务 |
| 2026-08-21 | v0.10.1 | v1.3.1 | 修复首次连接竞争、重复认证提示，以及 BLE 重连或空闲后虚拟麦克风无声 |
| 2026-08-20 | v0.10.0 | v1.3.0 | 原生 Codex Micro BLE HID、四 Agent 两段式触摸、顶部推理滑动、中心四向 Radial、重连恢复与 Agent 友好开发文档 |
| 2026-08-18 | v0.9.2 | v1.2.0 | 最近四小时活动方格恢复为每列上、下交替，短时间使用也能同时利用两排显示 |
| 2026-08-18 | v0.9.1 | v1.2.0 | 统一双 UI 的 A/B 语音交互，识别完成前不再误发送，并修正 Classic / Pet 首页预览 |
| 2026-08-18 | v0.9.0 | v1.2.0 | Codex 未读任务标题、滚动四小时活动热力图、续航实测与新版项目首页 |
| 2026-08-17 | v0.8.1 | v1.1.8 | 修复省电版本中的 BLE ATT 超时和反复断线，串行化 Bridge 写入并补齐进程守护 |
| 2026-08-16 | v0.8.0 | v1.1.7 | 整机省电与降温，将活动窗口调整为最近四小时 |
| 2026-08-16 | v0.7.4 | v1.1.6 | 录音断线明确报错并结束本次听写，重连后重新录制 |
| 2026-08-16 | v0.7.3 | v1.1.5 | 麦克风改为按需传输，设备键和 Mac 快捷键都可唤醒 |
| 2026-08-16 | v0.7.2 | v1.1.4 | OpenWatcher V2 差分刷新和 10 FPS 波形 |
| 2026-08-16 | v0.7.1 | v1.1.3 | 修复 OpenWatcher V2 的按键响应和再次录音 |
| 2026-08-16 | v0.7.0 | v1.1.2 | 实时 BLE 麦克风、虚拟输入和音频自恢复 |
| 2026-08-15 | v0.6.0 | v1.0.3 | 周额度、输入配置和 Bridge 稳定性改进 |
| 2026-06-13 | v0.5.0 | v1.0.0–v1.0.2 | 首个 Codex 页面、Pet、BLE Bridge 和输入模式 |

逐项变更记录见 [固件 Changelog](CHANGELOG.md) 和 [Bridge Changelog](tools/typeless_bridge/CHANGELOG.md)。

<details>
<summary><strong>从源码构建</strong></summary>

### 固件

需要 ESP-IDF v5.5.x 和 M5Stack StopWatch：

```bash
cd firmware-stopwatch-idf
python3 ./fetch_repos.py
idf.py set-target esp32s3
idf.py build
idf.py flash
```

首次安装或分区表发生变化时使用完整 `idf.py flash`；确认分区布局兼容后才使用 `idf.py app-flash`。

### macOS Bridge

```bash
tools/typeless_bridge/build_stopwatch_ble_bridge.sh
tools/typeless_bridge/install_launch_agent.sh
```

安装后在 `系统设置 → 隐私与安全性 → 辅助功能` 中允许 `StopWatch BLE Bridge`。

</details>

## 隐私

- 固件不保存 OpenAI、Typeless、微信输入法或其他云服务凭据。
- Mac Bridge 在本机读取当前用户的 Codex 状态，并向设备发送摘要。可选云同步只向用户自己配置的服务器发送额度读数、重置时间、伪匿名标识、录音起止时间和操作权重；活动时间仍属于个人数据。
- 麦克风音频只在本机通过 BLE 实时传给虚拟输入设备；本项目不生成 WAV 文件，也不提供云端录音服务。

## 开源许可

本项目的原创代码和修改内容使用 [MIT License](LICENSE)。固件所包含的第三方组件继续遵循各自许可证。

`M5 StopWatch Mic` 使用基于 BlackHole 的独立 Core Audio 驱动，该组件遵循 GPLv3；来源、固定提交和再分发要求见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 致谢

项目基于 M5Stack StopWatch UserDemo，并使用 ESP-IDF、LVGL、BlackHole 等开源项目。

特别感谢 [OpenWatcher](https://github.com/openwatcher-ai/openwatcher)：新版界面参考了该项目的 UI 设计思路，并结合 StopWatch 的圆形屏幕、额度展示和语音输入流程进行了重新设计。感谢 OpenWatcher 项目带来的启发，也感谢所有上游项目和贡献者。
