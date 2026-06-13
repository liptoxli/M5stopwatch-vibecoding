# Pet 形象替换指南

## 资产结构

Pet 资产最终会变成 LVGL 可直接引用的 C 文件：

```text
firmware-stopwatch-idf/main/assets/images/codex_pet_*.c
```

Codex 页面会根据状态播放不同帧组：

- `codex_pet_idle*`
- `codex_pet_blink*`
- `codex_pet_touch*`
- `codex_pet_processing*`
- `codex_pet_msg_*`

## 设计建议

- 先按 `466 x 466` 圆屏预览，不要按普通矩形屏幕构图。
- Pet 主体略高于圆心，底部要给状态胶囊和状态点留空间。
- 脸、眼睛、手是同一套坐标系统；调整位置时一起移动。
- 手部动作可以略微超出中央框，但不要碰到左右额度弧线。
- 使用透明背景，避免黑底边缘出现脏边。
- 每组动画帧保持画布尺寸一致，避免播放时跳动。

## 生成

脚本入口：

```bash
swift firmware-stopwatch-idf/tools/generate_codex_pet_assets.swift
```

根据脚本参数准备源图后生成 C 资产。生成后确认：

```text
firmware-stopwatch-idf/main/CMakeLists.txt
firmware-stopwatch-idf/main/apps/app_codex/view/view.cpp
```

仍引用正确文件名。

## 验证

```bash
cd firmware-stopwatch-idf
idf.py build
idf.py app-flash
```

在真机上重点检查：

- 眼睛和脸是否偏离主体。
- 手部动作是否清晰。
- 触摸、按键、摇晃、处理中状态是否能区分。
- 电量状态栏下拉时是否遮挡 Pet 触摸区域。
