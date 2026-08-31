# 安全和隐私

## 设备端

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

## macOS Bridge

Bridge 运行在用户 Mac 上，可以在用户开启相关开关后读取本机状态：

- Codex quota：读取 `~/.codex/auth.json` 并请求 Codex usage endpoint。
- Typeless：通过 Accessibility 读取窗口/焦点状态。
- BLE：连接 `M5Codex-*` 设备并写入状态摘要。

Bridge 不把原始 token 写入设备。建议日志只记录状态、错误类型和时间，不记录 Authorization header、Cookie 或原始响应。

## 虚拟麦克风输出

Bridge v1.3.6 将解码后的音频固定送入允许的虚拟设备，不以 Mac 扬声器作为备用出口。启动、录音前和运行中核验实际路由；路由异常时清零输出并结束本次听写，避免语音意外外放或恢复后补播旧音频。

音频在内存中实时传输，不生成 WAV 或本地录音文件。虚拟麦克风默认为关闭；更新 Bridge 时保留用户已有的开关、配对与权限。系统中出现 `M5 StopWatch Mic` 只说明驱动可见，不代表应用已经获得音频。

开发用回环测试只发送合成音并统计虚拟输入电平，但 macOS 仍可能要求测试程序的麦克风权限；是否授权由用户决定，不应通过修改系统权限数据库来绕过。
