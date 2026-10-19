# 蓝牙配对与恢复

适用固件：**v0.10.9 或更新版本**。正常连接时不需要定期重置，也不要把反复“忽略设备”当作日常步骤。

## 什么时候使用

Mac 已忽略设备、换了电脑，或反复连接仍失败时，电脑与 StopWatch 保存的配对信息可能不一致。仅退出 Mac Bridge 不会断开 macOS 管理的原生 HID 连接；需要切换电脑时，请先在原电脑的系统蓝牙设置中断开设备，避免它继续占用连接。

先保持设备开机、靠近目标 Mac，检查系统蓝牙状态。**只有确需重新配对时，才执行下列重置。** 麦克风无声但 HID 和配对正常，不应直接归因于密钥问题；分别检查 Bridge、音频订阅和虚拟输入电平。

## 操作步骤

1. 将固件升级至 v0.10.9 或更新版本。v0.10.8 的逐条清理方式仍可能被旧的重复订阅记录阻塞，不建议继续使用它恢复。
2. 在 StopWatch 打开 `Settings → Bluetooth → Reset pairing`。
3. 阅读确认页；点击 `Cancel` 不清除记录。点击 `Reset & restart` 才保存请求并重启。
4. 重启时完成蓝牙配对存储清理及核验，然后恢复广播。
5. 在目标 Mac 的系统蓝牙设置连接 `M5Codex-*`。若 Mac 仍保存旧配对，请忽略该设备一次，再重新连接。
6. 原生 HID 连接成功后，让 Bridge 接入 Companion 和麦克风服务，再分别验证按键、Agent 与语音输入。

此操作会移除 StopWatch 保存的**全部电脑绑定**，不只当前电脑；其他电脑之后也需要重新配对。Wi-Fi、主题、额度缓存和按键设置保持不变。不擦除整个 NVS 分区，不卸载或重装虚拟麦克风。

正常固件升级和普通重启不会清除配对。若此前已经在 v0.10.8 确认重置但卡住，v0.10.9 会在启动时完成保留的请求，无需再次确认。

## v0.10.9 修复的具体问题

现场读取到重复的 CCCD（蓝牙通知订阅）记录。ESP-IDF 5.5.4 的持久化删除通过 RAM 与 NVS 的内容差集寻找待删条目；多个值完全相同时，删掉内存中的一条后，同一值仍能匹配剩余记录，因而找不到对应的 NVS 删除项。

原来的 `ble_store_clear()` 会先清除安全密钥，再清订阅；卡在后一步时留下半清除状态，日志为：

```text
NIMBLE_NVS: NVS delete operation failed for CCCD
BLE-Bridge: explicit pairing reset: clear rc=28
BLE-Bridge: pairing reset incomplete: stage=2; request retained for retry
```

v0.10.8 为防止使用半清除记录连接而停止广播，但启动重试仍遇到同一组重复记录。因此重复 Reset 并不能自行修复它。

v0.10.9 只在用户确认后，在 `nimble_port_init()` 之前用 NVS namespace 操作清空专用的 `nimble_bond` 存储区；核验其为空，再完成 `ble_bridge/pair_reset` 请求。此时蓝牙内存缓存尚未加载，不会出现“Flash 已清、内存仍旧”的不一致。任何步骤失败都保留请求，不启动半重置的蓝牙。

另将全局 `CONFIG_BT_NIMBLE_MAX_CCCDS` 从 16 提高到 32，以容纳三台已绑定主机各约 10 条订阅，并加入编译断言。CCCD/CSFC 订阅表满时返回容量错误，不再通过默认回调静默删除另一台主机的密钥。安全记录本身超过三台主机容量时，仍保留原有轮换策略；本次不是增加同时连接多台电脑的功能。

最早为什么生成重复记录、历史上每次断连是否与此有关，仍缺少直接证据。本次确认并修复的是配对恢复路径，不能推定所有蓝牙、音频或环境干扰问题都已解决。

## 给 Agent / 二次开发者

| 文件或参数 | 作用 |
| --- | --- |
| `firmware-stopwatch-idf/main/apps/app_setup/workers/device.cpp` | 重置入口、二次确认、取消、保存请求失败提示和重启 |
| `main/hal/ble_bridge.cpp` 的 `request_pairing_reset()` | 仅持久化用户请求，不在活动配对回调中擦除 |
| 同文件的 `complete_requested_pairing_reset()` / `PairingResetStore` | 初始化前擦除、验证、完成请求；失败阻止启动 |
| `main/hal/pairing_reset_policy.h` | 请求 → 清除 → 验证 → 确认的可测试顺序 |
| `ble_bridge/pair_reset` | 待执行标记；`1` 待处理，验证完成后才为 `0` |
| `nimble_bond` | 只属于蓝牙配对库的 namespace，不能替换成全分区擦除 |
| `kBondSchemaVersion=2` | 本次保持不变，不能为修复个别配对强制清除所有用户绑定 |
| `MAX_BONDS=3`、`MAX_CCCDS=32` | 支持保存的主机数量与全局订阅容量，不代表可同时连接三台 |

表中 `main/` 路径均相对 `firmware-stopwatch-idf/`。不要把清理移回 `BLE_GAP_EVENT_REPEAT_PAIRING` 回调，不要在 NimBLE 已恢复内存记录后直接擦除 namespace，也不要用“错误就全量清空 NVS”来替代恢复策略。

在仓库根目录执行：

```bash
bash firmware-stopwatch-idf/tests/run_pairing_reset_policy_tests.sh
bash firmware-stopwatch-idf/tests/run_voice_start_gate_tests.sh
tools/check_version.sh
```

第一个测试覆盖正常启动保留配对、用户确认后清理、清理失败/验证失败/确认失败保留请求和重试；它是状态机测试，**不模拟真实 ESP-IDF NVS 删除实现**。真实存储恢复必须结合 USB 日志和重新配对验收，不能仅凭单测通过宣称完成。

成功日志应包含：

```text
BLE-Bridge: explicit pairing reset: namespace clear=ESP_OK
BLE-Bridge: explicit pairing reset: namespace empty=1 result=ESP_ERR_NVS_NOT_FOUND
BLE-Bridge: explicit pairing reset complete; ready for new pairing
BLE-Bridge: host synced: ... bonds=0 rc=0 cccds=0 cccd_rc=0
BLE-Bridge: HID advertising started
```

最后的 `ESP_ERR_NVS_NOT_FOUND` 在这个核验中表示 namespace 中没有条目，属于成功结果。重新配对后 bond 和订阅数增加是正常现象；普通重启不应再次出现 reset complete。

## 本次验证与隐私

2026-10-19：构建与 USB 刷写通过，Flash 哈希校验成功，实机内部版本为 v0.10.9。重置后请求标记为 0，密钥与订阅数为 0，广播恢复；其他应用 namespace 的 65 条存储项逐项比对一致。用户在 Mac mini 重新连接后确认恢复正常。

本次未重新进行双机、长语音、全部按键或配对后断电重连的完整验收；这些仍是后续回归项目。也没有把默认订阅溢出策略的潜在风险写成已经发生过的现场事件。

原始 NVS 备份可能同时包含蓝牙密钥、Wi-Fi 和个人配置，**不得提交 Git、上传 Release 或作为公开日志附件**。公开诊断只应保留错误码、数量、版本和脱敏状态，不包含完整 NVS、主机地址、用户 UUID 或账号凭据。
