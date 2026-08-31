# StopWatch 跨设备共享统计（Bridge 1.4.0）

可选的自托管共享统计：多台 Mac 共用同一份额度与设备活动历史，也可供其他授权客户端读取。
服务地址和部署位置由用户配置，不绑定维护者的服务器，不内置真实凭据。
可以把统计模块接入已有 API，或运行仓库中的 [独立服务示例](../server/cloud-sync/README.md)。

## 数据怎么走

```text
各 Mac 本地 Codex 登录 → 官方周额度观察值       ┐
各 Mac 的 StopWatch 录音时段、操作事件          ├→ 自托管统一历史 → 各 Mac → BLE 表盘
未来其他采集端                               ┘                 → 其他授权读取端
```

- OpenAI 登录凭据只留在 Mac。云端只保存额度读数、重置时间、账号范围哈希、伪匿名设备 ID、录音起止及操作权重。活动时间仍属于个人数据。
- 不上传音频、识别文字、聊天内容、项目标题或任务正文。
- 剩余量是官方账号同一额度池的观察值，不是设备本地的“扣费”。手机等其他入口计入同一池的消耗也会反映到剩余量。
- 热力图只表示本系统实际记录的设备活动，不能从额度下降推算手机操作或 Codex 工作频率。

## 统一口径

| 项目 | 规则 |
| --- | --- |
| Today 起点 | 每天北京时间 08:00（Asia/Shanghai；UTC 00:00），不随电脑时区改变 |
| Today 单位 | 当天消耗的周额度百分点；不是每天单独重置的一份官方额度 |
| 剩余量 | 同一账号最新有效观察，晚到的旧记录不能覆盖新值 |
| 重复上报 | 按设备＋事件 ID 去重；同一官方下降只计一次，不相加两台 Mac 的 Today |
| 周重置 | 参考官方 reset_at；不把任意上涨都当作补满，无法解释的变化标记数据不完整 |
| 热力图 | 滚动 4 小时、24 格，每格 10 分钟；保持旧的上下交替显示和颜色 |
| 强度 | 70% 录音时长占比＋30% 开始次数/操作权重（频率目标每格 4 次）；0–1，保留两位小数 |
| 录音重叠 | 合并重叠时段，避免交接两台 Mac 时把一次录音算两次 |

`primary_down` 权重 0.25；`enter`、`shake` 权重 0.15。同步、心跳不算活动。
上传活动采用稳定事件 ID；录音中的 checkpoint 只延长同一段结束时间，旧 checkpoint 不会缩短它。

### 缺失历史不能当作 0

- `sampled`：08:00 附近 10 分钟范围内有观察值，按观察值计算；依然有采样间隔误差。
- `imported`：从旧 Mac 本地记录导入的估算起点。表盘提示 `Today estimate`。
- `partial`：中途才开始、没有边界基线，或额度存在无法解释的变化。提示 `Today incomplete`，Today 显示 `--%`。
- `missing`：本统计日没有可用记录。不会拿昨天的 Today 或 0 冒充今天。

旧记录只在首次初始化云端缓存时导入；两机起点不直接相加。旧日跟踪有重置、时区边界不符或格式异常时不猜测迁移。
只要有一个有效采集端持续运行，就不要求手表保持连接。若所有 Mac 睡眠/关机，云端本身不能凭空获得官方新读数。

## API（供其他系统和 Agent 使用）

默认示例使用 `GET /api/rlcd/codex`，携带读写或只读凭据后返回 `ok`、`codex` 和 `shared_usage`。
未认证请求被拒绝。接入已有服务时可以保留其原有字段，但不要因此公开活动和 Today 历史。

```json
{
  "ok": true,
  "codex": {"valid": true, "weekly_left_pct": 88, "weekly_usage": "09-07 10:30"},
  "shared_usage": {
    "version": 1,
    "as_of": 1788193600,
    "stale": false,
    "weekly": {"valid": true, "left_pct": 88, "reset_at": 1788748228, "observed_at": 1788193596},
    "daily_tracking": {
      "boundary_hour_local": 8,
      "timezone": "Asia/Shanghai",
      "day_key": "2026-08-31",
      "period_start": "2026-08-31T00:00:00.000Z",
      "day_start_left_pct": 98,
      "used_since_start_pct_points": 10,
      "quality": "imported"
    },
    "activity_window_seconds": 14400,
    "activity_buckets": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.3]
  }
}
```

示例省略部分辅助字段；使用方校验 `version`、数值范围、`stale`、`quality`、24 格长度。
`as_of` 是该份滚动热力图的计算时间；不同时间读取的格子会自然滚动，不保证不同分钟逐字节相同。

统计写入仍是 `POST /api/rlcd/codex`，使用专用 `CODEX_SYNC_TOKEN`：

```json
{
  "shared_usage": {
    "version": 1,
    "account_scope": "<account hash>",
    "device_id": "<stable device UUID>",
    "observations": [{"id":"<stable sample UUID>","at":1788193596,"left":88,"reset_at":1788748228}],
    "activity": [{"id":"<stable event ID>","kind":"recording","start":1788193500,"end":1788193560,"units":0}]
  }
}
```

- `Authorization: Bearer <专用凭据>`；`Content-Type: application/json`。
- 读写钥匙仅允许这个账号的统计，不应复用其他业务的管理员凭据。
- 可选 `CODEX_SYNC_READ_TOKEN` 仅允许读取新增统计，不能 POST；未来只显示数据的设备应使用只读权限。
- 不给固件配置通用服务器写入 token 或 OpenAI token。
- `account_scope` = SHA-256(`m5-codex-account-v1:` + 本地 Codex `tokens.account_id`)；服务端配置固定账号范围，拒绝错账号。
- 时间使用 Unix 秒，不是毫秒。`reset_at` 未知时必须为 JSON `null`。
- 每批最多 3000 条额度、4000 条活动；正文最大 2 MiB。超前服务器超过 120 秒、超过保留期或越界数值会拒绝整批。
- 现有采集器可以调用 `ingestLegacyQuota()`，但宿主 API 必须自行保留原鉴权，并传入匹配的 `account_scope` 和有效额度字段；独立示例只接受上面的 `shared_usage` 格式。
- 服务端 SQLite 事务和唯一键保证重试、并发和进程重启安全。默认保留 8 天，支持当前日计算和离线补传。

## Bridge 配置与离线策略

私有文件：`~/Library/Application Support/M5StopWatch/StopWatchBleBridge/cloud-sync.json`，权限 `600`。

```json
{
  "endpoint": "https://sync.example.com/api/rlcd/codex",
  "token": "<dedicated sync credential>",
  "account_scope": "<account hash>",
  "device_id": "<unique stable UUID per Mac>"
}
```

可用 `tools/typeless_bridge/prepare_cloud_sync.py` 在仓库外生成私有配置，具体步骤见 [服务端 README](../server/cloud-sync/README.md)。两台 Mac 使用相同账号范围和读写凭据，但必须有不同的稳定设备 ID。
将各自配置放到上述路径，设置权限 600，重启 Bridge，并确认已启用“推送 Codex 额度”。
无此文件则保持本地模式。不要把真实配置、私钥或 token 提交 GitHub。
`cloud-sync-state.json` 保存同账号已确认的快照和未确认额度样本；只在有效响应后清理已发送项。
账号不同会停止同步，不覆盖旧账号缓存。损坏的缓存不会被悄悄清空。

- 云交换 60 秒一次；额度采集沿用设置，默认 300 秒，在跨 08:00 边界时额外采集。
- 云请求超时 8 秒、最长资源时间 10 秒；拒绝 HTTP 重定向。
- BLE 面板仍按原额度间隔发送；录音中延后面板发送。不改变音频包、A/B 键、触摸、Agent 逻辑。
- 网络不可用时保留缓存并标记 stale，待上传额度保留 7 天。活动历史由原本地 4 小时记录重传。
- 崩溃前未上传的最后一小段正在录音时长无法从音频恢复；系统不保存音频用于补录。

## 代码入口与验证

| 模块 | 入口 |
| --- | --- |
| 云端统计、持久化、去重 | `server/cloud-sync/lib/codex-shared-usage.ts` |
| HTTP 适配与独立启动 | `server/cloud-sync/lib/http.ts`、`server.ts` |
| Bridge 云客户端 | `tools/typeless_bridge/stopwatch_cloud_sync.swift` |
| 活动导出、BLE 发送与独立采集定时器 | `tools/typeless_bridge/stopwatch_ble_bridge.swift` |
| 私有配置准备 / 只读检查 | `tools/typeless_bridge/prepare_cloud_sync.py` / `cloud_sync_status.py` |

```bash
npm --prefix server/cloud-sync test
bash tools/typeless_bridge/tests/run_cloud_sync_tests.sh
bash tools/typeless_bridge/tests/run_audio_route_tests.sh
bash tools/typeless_bridge/build_stopwatch_ble_bridge.sh
python3 tools/typeless_bridge/cloud_sync_status.py
```

统计模块基于 Node 22.22+ 内置 SQLite；Node 22/24 可能提示该模块仍属实验 API，这不是测试失败。
发布测试使用临时数据库和测试凭据，不指向用户的生产账号。真实跨日、睡眠唤醒与长时间离线仍需按使用环境验收。

## 运维与回退

部署前备份自己的应用、配置和数据库，在隔离环境验证 API。运行时配置保存在仓库外或被忽略的 `.env`；启动账号只需访问专用数据目录，不要给它其他业务数据库权限。

暂停同步：退出 Bridge，把 `cloud-sync.json` 移到私有备份目录，再启动 Bridge 即恢复本地模式；不会自动把完整云历史迁回本地。保留 `cloud-sync-state.json`，以后可继续同账号同步。不要在账号切换时直接复用旧缓存。

回退：恢复自己的上一版 Bridge 与原配置，不删除云数据库；保持原签名，不改 HAL、蓝牙配对、快捷键或系统默认扬声器。停止云端进程后再备份 SQLite，或使用 SQLite 一致性备份；运行中不要只复制主文件而漏掉 WAL。

当前服务实例固定一个 Codex 账号范围，不提供公网多租户注册、登录或管理后台。新设备可以读取该账号数据；不同账号应使用独立实例、数据库与凭据，不能混入同一份统计。
