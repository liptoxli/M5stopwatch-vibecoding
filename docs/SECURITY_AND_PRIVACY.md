# 安全和隐私

## 固件边界

固件不保存 AI 服务凭据，不读取桌面账号状态，也不要求用户输入 token。

固件可以保存：

- BLE HID 绑定
- 输入模式
- 设备设置
- 最近一次收到的额度摘要

固件不应该保存：

- OpenAI / ChatGPT / Codex access token
- Claude / Anthropic token
- 浏览器 Cookie
- macOS Keychain 内容
- Typeless 或微信输入法账号信息

## macOS Bridge 边界

Bridge 运行在用户 Mac 上，可以在用户开启相关开关后读取本机状态：

- Codex quota：读取 `~/.codex/auth.json` 并请求 Codex usage endpoint。
- Typeless：通过 Accessibility 读取窗口/焦点状态。
- BLE：连接 `M5Codex-*` 设备并写入状态摘要。

Bridge 不把原始 token 写入设备。建议日志只记录状态、错误类型和时间，不记录 Authorization header、Cookie 或原始响应。

## 开源版脱敏内容

公开目录已排除：

- 本地 `.env`
- 内部 server
- 私有 relay URL
- 私有 Wi-Fi
- 构建产物
- 历史 handoff
- 本机绝对路径

发布前建议再跑：

```bash
rg -n "token|secret|Authorization|api\\.your-domain|/Users/" .
```
