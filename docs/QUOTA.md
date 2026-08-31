# 额度获取机制

## 原则

StopWatch 固件只显示额度摘要，不直接登录任何 AI 服务。

不应该放进固件的内容：

- OpenAI / ChatGPT / Codex token
- Claude / Anthropic token
- 浏览器 Cookie
- macOS Keychain 读取逻辑
- 用户本机原始日志

固件只接收：

- 剩余额度百分比
- 已用/剩余文本
- 重置时间
- 数据新鲜度
- 错误或降级状态

## Codex 额度

当前实现位于 macOS Bridge：

```text
tools/typeless_bridge/stopwatch_ble_bridge.swift
```

当用户启用“推送 Codex 额度”后，Bridge 会读取：

```text
~/.codex/auth.json
```

然后调用：

```text
https://chatgpt.com/backend-api/wham/usage
```

Bridge 不保存 access token，只使用本机已有登录文件即时请求。请求结果被归一化为设备 payload，再通过 BLE GATT 写入设备的 panel characteristic。

当前设备 contract 只接受 604800 秒的 weekly window，不显示或回退到其他窗口。Bridge v1.4.0 的日界线固定为北京时间 08:00 到次日 07:59，不再跟随电脑时区。未配置云同步时，Bridge 在本机维护以下统计：

- `day_start_left_pct`：本周期第一次成功采样时的周剩余额度。
- `segment_start_left_pct`：当前周额度段的起点。
- `current_left_pct`：当前周剩余额度。
- `used_since_start_pct_points`：本周期累计消耗百分点。
- `reset_count`：本周期检测到的周额度重置次数。

如果周额度在日统计周期内重置，剩余额度向上跳变不会被当作负消耗；Bridge 会开始新的统计段并累计 `reset_count`。如果 Bridge 在 08:00 没运行，无法回溯边界到首次成功采样之间的精确消耗。

### 可选跨 Mac 同步

配置 `cloud-sync.json` 后，服务端合并同一账号的额度观察值与设备活动，不把各 Mac 的 Today 相加。最新官方剩余量、固定日界线的累计消耗、四小时热力图由同一份历史生成，再交给原 BLE 面板通道。

Today 是“周额度百分点的当天消耗”，不是官方每日独立额度。云模式对缺失 08:00 基线、旧记录导入和异常上涨分别标注不完整或估算。其他终端的消耗只有在计入同一官方额度池时才会反映到剩余量；不能据此恢复所有终端的操作时间。

服务地址和权限自行配置，无维护者预设服务器。详见 [跨设备同步与 API](stopwatch-cloud-sync.md)。

设备侧接收路径：

```text
firmware-stopwatch-idf/main/hal/ble_bridge.cpp
firmware-stopwatch-idf/main/apps/app_codex/codex_quota_client.cpp
```

如果 Bridge 不运行，设备仍可显示上一次状态或本地错误状态。Wi-Fi fallback 默认关闭。

## Claude Code 额度

Claude Code 额度建议按 macOS 本机工具方式实现，参考：

```text
https://github.com/zhuchenxi113/ai-limit
```

参考重点不是 UI 复刻，而是数据处理方式：

- 在 Mac 侧读取本机 Claude Code 或浏览器已有登录/使用状态。
- 优先使用只读、本机、用户已授权的数据来源。
- 把原始结果转成统一 quota snapshot。
- 只把 snapshot 推送给设备。
- 数据来源失效时显示 degraded/unavailable，而不是静默显示旧值。

建议的统一 snapshot：

```json
{
  "service": "claude-code",
  "window": "daily-or-plan-window",
  "remainingPercent": 72,
  "resetAt": "2026-06-14T08:00:00Z",
  "source": "local-macos-collector",
  "freshnessSec": 20,
  "status": "ok"
}
```

如果要把 Claude Code 接入本项目，推荐新增 macOS collector，再复用 Bridge 的 BLE 推送通道。不要让固件直接调用 Claude、Anthropic 或浏览器接口。

## Wi-Fi fallback

固件保留 Wi-Fi panel client，便于高级用户接自己的服务：

```text
firmware-stopwatch-idf/main/apps/app_codex/codex_config.h
```

默认值：

```text
kDefaultWifiEnabled = false
kDefaultWifiQuotaFallbackEnabled = false
```

启用前需要自行提供：

- panel URL
- Wi-Fi SSID/password
- 服务端返回的稳定 JSON contract

如果你只使用 Mac 附近的桌面场景，建议保持 Wi-Fi 关闭，使用 BLE 推送。
