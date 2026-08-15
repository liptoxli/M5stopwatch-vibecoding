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

发布前执行：

```bash
tools/check_version.sh
cd firmware-stopwatch-idf
idf.py build
```

合并到 `main` 后创建同名标签和 GitHub Release，例如 `v0.6.0`。不得重复使用或移动已经公开的版本标签；新改动先记录在 `CHANGELOG.md` 的 `Unreleased`，发布时再转入新版本条目。
