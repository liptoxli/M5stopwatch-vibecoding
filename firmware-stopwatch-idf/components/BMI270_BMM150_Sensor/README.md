# BMI270_BMM150_Sensor

这是基于 ESP-IDF 的 BMI270 + BMM150 传感器使用示例与组件工程。

当前目录用于集中管理：

- 组件源码（`BMI270_BMM150_Sensor`）
- 示例工程（如 `simple_usage`）

## 目录说明

- [BMI270_BMM150_Sensor](BMI270_BMM150_Sensor)
	- 传感器组件源码（`include/`、`src/`）
	- 组件清单 [BMI270_BMM150_Sensor/idf_component.yml](BMI270_BMM150_Sensor/idf_component.yml)
	- 示例目录 [BMI270_BMM150_Sensor/examples](BMI270_BMM150_Sensor/examples)
- [README.md](README.md)
	- 当前总览文档

## 环境要求

- ESP-IDF `>= 5.3`
- 可用串口与目标开发板
- I2C 连接的 BMI270/BMM150 传感器模组

## 快速开始

进入示例并构建运行：

```bash
cd BMI270_BMM150_Sensor/examples/simple_usage
idf.py set-target <your_target>
idf.py build
idf.py -p <your_port> flash monitor
```

示例说明详见：

- [BMI270_BMM150_Sensor/examples/simple_usage/README.md](BMI270_BMM150_Sensor/examples/simple_usage/README.md)

## 组件能力（当前示例已覆盖）

- 通过 `bmi270_bmm150_sensor_create()` 初始化设备
- 读取加速度：`bmi270_bmm150_sensor_read_acceleration()`
- 读取陀螺仪：`bmi270_bmm150_sensor_read_gyroscope()`
- 读取磁场：`bmi270_bmm150_sensor_read_magnetic_field()`

## 常见排查

- 构建失败：先执行 `idf.py reconfigure` 后重试。
- 设备无数据：检查 I2C 引脚、电源地线、地址配置。
- 串口无日志：确认端口号与波特率设置正确。
