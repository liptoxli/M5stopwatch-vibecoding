# 可选的跨设备统计服务

把多台 Mac 的 Codex 额度观察值与 StopWatch 活动合并为同一份统计。没有维护者的服务器地址、账号或凭据；你可以自行部署，也可以把 `lib/codex-shared-usage.ts` 接进已有 API。

此目录是可独立运行的 Node 示例，不依赖个人看板、天气服务或 Next.js。服务端不会登录 OpenAI；至少有一台已登录 Codex、开启额度同步的 Mac 在运行时，才会有新的官方额度样本。

## 开始前

- Node.js 22.22+，使用内置 SQLite，无需安装第三方运行依赖。
- 一个由自己控制、具有有效证书的 HTTPS 入口，反向代理到本机服务。
- 每个服务实例只服务一个 Codex 账号，不是公网多租户平台。
- 两台 Mac 登录同一 Codex 账号，安装 Bridge v1.4.0。已有固件 v0.10.6 不必重刷。

## 1. 本地生成私有配置

在已登录 Codex 的 Mac 上，从仓库根目录运行；替换域名为自己的 HTTPS 地址，输出目录必须是仓库外的**新目录**：

```bash
python3 tools/typeless_bridge/prepare_cloud_sync.py \
  --endpoint https://sync.example.com/api/rlcd/codex \
  --output /private/tmp/my-stopwatch-sync \
  --devices 2
```

工具只在本机读取账号 ID 来计算范围哈希，生成随机凭据和每台设备不同的 UUID；不联网、不安装、不打印凭据。生成目录权限 700、文件 600，拒绝覆盖旧目录。

- `server.env`：只交给自己的服务器管理员，包含账号范围、读写及只读凭据。
- `device-1-cloud-sync.json`、`device-2-cloud-sync.json`：分别交给两台 Mac；不要两台用同一个文件。

用受信任的加密方式传送文件，不贴到 issue、聊天截图或公开仓库。后续增加 Mac 时复用该实例的账号范围和读写凭据，为新 Mac 生成新 UUID；不要重新生成整套凭据而忘记更新原设备。

## 2. 在自己的服务器运行

将生成的 `server.env` 安全放入本目录为 `.env`（Git 已忽略），或使用进程管理器注入相同变量：

```bash
cd server/cloud-sync
chmod 600 .env
npm test
npm start
```

不需要 `npm install`。测试只用临时数据库和合成数据。默认监听 `127.0.0.1:4199`，不直接对外暴露；自行配置 HTTPS 反向代理，将 `/api/rlcd/codex` 转到该服务。反向代理应保留 Authorization、禁用缓存、允许最多 2 MiB 请求，并设置合理限流；不要记录请求正文或 Authorization。

| 参数 | 用途 |
| --- | --- |
| `CODEX_SYNC_ACCOUNT_SCOPE` | 本地账号派生的 SHA-256 范围，不是原始账号 ID |
| `CODEX_SYNC_TOKEN` | 至少 32 字符的随机读写凭据，仅用于该统计 API |
| `CODEX_SYNC_READ_TOKEN` | 可选、不同的随机只读凭据；不能用于 Bridge 写入 |
| `CODEX_SYNC_DATA_DIR` | 数据目录，默认 `./data`，建议生产环境使用自己的持久目录 |
| `CODEX_SYNC_DB_PATH` | 可选，直接指定 SQLite 路径，优先于数据目录 |
| `PORT` | 本机监听端口，默认 4199 |

示例 `.env.example` 只有占位符，不能直接作为可用凭据。使用非 root 专用进程账号、限制数据目录权限，并自行配置进程守护。仓库不替你操作服务器或改变其他服务。

## 3. 配置 Mac

退出 Bridge，将每台各自的 JSON 重命名为 `cloud-sync.json`，放到：

```text
~/Library/Application Support/M5StopWatch/StopWatchBleBridge/cloud-sync.json
```

设置文件权限 600，重新启动 Bridge，确认“推送 Codex 额度”已开启。没有该文件时仍使用原本地模式。账号不匹配时同步会停止，不会自动混合另一账号的记录。

仓库根目录可执行只读检查：

```bash
python3 tools/typeless_bridge/cloud_sync_status.py
```

正常时 `cloud_access` 为 `ok`、`cells` 为 24、`pending_observations` 在同步完成后降为 0。查看 `stale` 和 `quality`，不要只看请求成功。状态输出包含用量，分享诊断信息前也应自行检查。

## 接到已有服务

保留自己的部署流程，在服务端调用：

- `syncAuthorized(request)`：专用写入权限校验。
- `syncReadAuthorized(request)`：读写或只读权限校验。
- `ingestSharedUsage(body.shared_usage)`：验证、持久化并返回当前统计。
- `readSharedUsage()`：读取同账号快照。

响应至少包含 `{ ok: true, shared_usage: ... }`，错误使用非 200 状态。请求大小、HTTP 方法、账号范围和授权限制不能省略。若已有接口还服务其他业务，应保持其原鉴权边界；不要把统计 token 提升为通用写入权限。

详细数据口径、API 示例、离线边界与回退见 [跨设备同步说明](../../docs/stopwatch-cloud-sync.md)。统计算法与 Bridge 用于实际双机同步的版本一致；独立 HTTP 适配层另有本机测试，并未代表所有服务器环境已实测。
