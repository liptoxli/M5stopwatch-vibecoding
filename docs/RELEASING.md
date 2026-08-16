# 发布与版本规则

项目使用语义化版本 `MAJOR.MINOR.PATCH`：

- `MAJOR`：协议或使用方式发生不兼容变化。
- `MINOR`：新增向后兼容的功能或明显的产品迭代。
- `PATCH`：修复问题且不改变既有行为。

当前版本必须同时出现在以下位置：

1. 根目录 `VERSION`。
2. `firmware-stopwatch-idf/version.txt`，供 ESP-IDF 写入应用元数据。
3. `firmware-stopwatch-idf/main/apps/common/common.h`，供设备启动页和 About 页面显示。
4. `README.md` 的版本徽章和当前版本说明。
5. `CHANGELOG.md` 的对应版本条目。

macOS Bridge 使用独立版本，单一版本源为 `tools/typeless_bridge/VERSION`。发布 Bridge 时还必须同步更新：

1. `tools/typeless_bridge/CHANGELOG.md`。
2. 根目录 `README.md` 的兼容版本表。
3. `tools/typeless_bridge/README.md` 的示例产物名。

发布前执行：

```bash
tools/check_version.sh
cd firmware-stopwatch-idf
idf.py build
```

首次安装或分区表/Bootloader 发生变化时必须执行完整 `idf.py flash`。`idf.py app-flash` 只用于已经具有兼容分区布局的设备。

合并到 `main` 后创建同名标签和 GitHub Release，例如 `v0.6.0`。不得重复使用或移动已经发布的版本标签；新改动先记录在 `CHANGELOG.md` 的 `Unreleased`，发布时再转入新版本条目。

Bridge 延续既有 `vMAJOR.MINOR.PATCH` 标签；当前固件仍处于 `v0.x`、Bridge 处于 `v1.x`，因此标签不会冲突。GitHub Release 中的 PKG 需要 Developer ID Installer 签名和 notarization；随 PKG 同时发布固定 BlackHole 提交的对应源码归档与 SHA-256。
