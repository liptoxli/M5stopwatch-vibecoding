# 功能说明书

## 1. 系统组成

本项目分成两层：

- 固件层：运行在 M5Stack StopWatch，负责圆屏 UI、宠物动画、电量状态、BLE HID、BLE GATT 配置接收、按键和 IMU 交互。
- macOS Bridge：运行在 Mac 菜单栏，负责连接设备、读取本机 Codex 额度、检测 Typeless 状态、保存用户按键绑定，并把配置同步到固件。

设计边界是：账号、Cookie、token、桌面焦点都只属于 Mac；设备只接收已脱敏的状态和 HID 配置。

## 2. Codex 页面布局

圆屏按 `466 x 466` AMOLED 设计：

- 顶部：当前时间，使用低调发光样式。
- 左侧弧线：Codex 5 小时额度。
- 右侧弧线：Codex 周额度。
- 中央：Pet 主体和状态动画，整体位置略高于圆心。
- 中下：今日在线/状态胶囊。
- 底部：BLE、Wi-Fi、语音和错误状态点。
- 顶部下拉层：电量状态栏。向下滑显示，向上滑隐藏。

电量策略：

- USB 充电插入时短暂显示电量。
- 20% 以下红色常驻。
- 用户可以上滑隐藏 20% 低电提示；隐藏只影响当前提示，不改变电量监测。

## 3. Codex 额度获取机制

推荐路径是 macOS Bridge 通过 BLE 推送：

1. 用户在 Mac 上安装并登录 Codex。
2. Bridge 可选读取 `~/.codex/auth.json`。
3. Bridge 调用 `https://chatgpt.com/backend-api/wham/usage`。
4. Bridge 把 5 小时额度、周额度、重置时间、状态转换成设备面板 payload。
5. 固件通过 BLE GATT 接收 payload 并更新 Codex 页面。

固件里仍保留 Wi-Fi panel fallback，但公开版默认关闭：

```text
kDefaultWifiEnabled = false
kDefaultWifiQuotaFallbackEnabled = false
```

如果你要启用 Wi-Fi，需要在 `firmware-stopwatch-idf/main/apps/app_codex/codex_config.h` 填入自己的 panel URL 和 Wi-Fi 信息。

## 4. Claude Code 额度获取办法

Claude Code 额度建议只在 macOS 端实现，不放进固件。

参考方式：

- 参考开源项目 `ai-limit` 的 macOS 额度监控思路。
- 读取本机已有登录状态或本机使用记录。
- 在 Mac 端归一化成“剩余额度、窗口重置时间、数据来源、错误状态”。
- 只把摘要推送给 StopWatch，不把 Claude 登录凭据、Cookie、API key、原始日志写入设备。

本项目内的参考文档见 [QUOTA.md](QUOTA.md)。其中 Claude Code 部分只规定集成边界；具体抓取逻辑应参考 `ai-limit` 或同类 macOS 本机监控工具。

## 5. Pet 建立机制

Pet 不是远程图片，也不是运行时下载资源。它是编译进固件的多帧 LVGL 图片资产：

- 源图或帧图放在 `docs/assets/` 或自定义工作目录。
- 生成脚本把图片转成 `firmware-stopwatch-idf/main/assets/images/*.c`。
- Codex view 根据状态选择帧组播放。

当前状态组包括：

- idle：默认呼吸/待机。
- blink：眨眼。
- touch：触摸反馈。
- processing：输入或处理中。
- msg idle/touch/shake/key/error：消息类动作和反馈。

UI 里 Pet 的框和脸整体略高于圆心，眼睛、脸和双手属于同一套图形坐标，移动时应该一起移动。

## 6. 更换 Pet 形象

推荐流程：

1. 准备统一画布尺寸和透明背景的多帧 PNG。
2. 保持脸、眼睛、手部动作在同一坐标系统里。
3. 用 `firmware-stopwatch-idf/tools/generate_codex_pet_assets.swift` 生成 C 资产。
4. 确认生成文件名仍匹配 `main/CMakeLists.txt` 和 Codex view 引用。
5. `idf.py build`，在设备上检查圆屏裁切、触摸反馈和动画节奏。

更详细步骤见 [PET_CUSTOMIZATION.md](PET_CUSTOMIZATION.md)。

## 7. macOS App 功能

Bridge 菜单栏应用提供：

- 蓝牙连接 `M5Codex-*` 设备。
- 中文设置界面。
- 输入模式切换：Typeless / 微信输入法。
- A 键、B 键、摇晃动作自定义绑定。
- F13-F20 固定候选键保留，适合绑定不干扰正常输入的快捷键。
- 其他键可通过用户键盘捕获生成自定义绑定。
- 保存每个输入模式自己的绑定配置。
- 切换模式时恢复对应绑定，并同步到固件。
- Codex 额度推送开关和刷新间隔。
- 可选 Typeless 快捷键同步。
- 开机自启动，不强制保活。

## 8. Typeless 和设备状态栏

Typeless 模式下，Bridge 使用 Accessibility 做三件事：

- 记录触发前的输入焦点。
- 识别 Typeless 录音、处理中、完成等状态。
- 必要时恢复焦点，并延迟发送确认键，避免文字还没插入就回车。

设备状态栏会显示 BLE、语音、额度和错误状态。Accessibility 权限缺失时，Bridge 会进入 limited 状态，设备仍可通过 BLE HID fallback 使用基础按键。

## 9. 按键绑定同步到固件

用户在 App 改动绑定后，Bridge 会把配置写到设备：

- input mode
- primary key / A 键
- confirm key / B 键
- shake action
- 每个键的 HID usage

固件收到后保存到 NVS。这样 App 退出后，设备仍能按最后一次同步配置发送基础 BLE HID。焦点恢复、Typeless 状态识别、Codex 额度推送这些能力仍需要 App 运行。

## 10. 省电机制

当前固件包含分级省电：

- 轻度省电：空闲后降低屏幕亮度。
- 省电模式：进一步降低负载，关闭不必要更新。
- 深度睡眠：接近关机，依赖按键或电源事件恢复。

Wi-Fi 默认关闭；音频输出保留，音频输入关闭。这样保留开机/按键音效，同时减少麦克风链路功耗。
