# ESP32-DIV IDF v2

ESP32-DIV 的 ESP-IDF 版本工程，面向 ESP32-S3 平台，提供统一的无线工具、显示界面、存储管理和设备配置能力。

## 项目结构

- `main/`：应用入口、功能模块和平台抽象实现
- `components/`：触摸屏、I/O 扩展器等外设组件
- `managed_components/`：通过 ESP-IDF Component Manager 管理的依赖
- `hardware/`：硬件资料
- `docs/`：设计文档和说明
- `assets/`：图片与静态资源

## 构建与烧录

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## 主要能力

### Wi-Fi

- 网络扫描与监测
- 抓包与 PCAP 记录
- Beacon 相关测试
- Deauth 检测与测试
- Captive Portal 页面
- Probe 请求测试

### Bluetooth / BLE

- BLE 扫描与嗅探
- BLE 广播与伪装
- BLE HID 键盘脚本
- 蓝牙干扰测试

### 2.4GHz

- 2.4GHz 频谱扫描
- 协议干扰与测试

### Sub-GHz

- 信号抓取与重放
- 干扰测试
- 配置文件管理

### 红外

- 红外信号采集与重放
- 遥控器配置管理
- SD 卡导入与收藏

### RFID / NFC

- 读卡与标签识别
- 支持写入标签的克隆与擦写
- 数据导出和解析
- 受控的干扰与测试流程

### GPS

- 卫星状态查看
- 位置记录与 Wardriving 日志
- Wi-Fi / BLE 观察数据记录

### 设备与系统

- 串口控制台
- SD 文件管理器
- 固件升级
- 触摸校准
- 系统设置

## 硬件说明

项目以主板 + 扩展板的方式组织，具体板型由 `main/board_config.h` 和 Kconfig 选择。

### 主板

- ESP32-S3 或 ESP32 系列主控，视板型而定
- ILI9341 或 ST7796 / ST7796S 显示屏
- 触摸屏控制器
- 电池充电与电源管理
- USB 转串口芯片
- SD 卡槽
- 按键、蜂鸣器、NeoPixel 等外设

### 扩展板

- NRF24 模块
- CC1101 模块
- 红外收发器
- 外接天线接口

## 设计说明

- 运行入口在 `main/app.cpp`
- 显示抽象在 `DivDisplay` 和 `DivSprite`
- SD 卡抽象在 `DivSd` 和 `DivFile`
- 持久化存储抽象在 `DivNvStore`
- 界面与功能模块共享 `featureClearContent()` 和 `drawStatusBar()` 等公共接口

## 存储路径

- `/config/settings.json`：应用设置
- `/captive_portal/captured.csv`：门户页面相关记录
- `/subghz/profiles_current.bin`：当前 Sub-GHz 配置
- `/ir/*.ir`：红外配置
- `/ducky/*.duck`：BLE Ducky 脚本
- `/wd_*.csv`：Wardriving 日志
- `/firmware.bin`：SD 卡固件升级文件

## 注意事项

- 本项目仅用于教育、研究和授权测试
- 请勿在未授权的网络或设备上使用相关功能
- 使用前请确认当地法律法规

## 许可证

本项目采用 MIT 许可证，详见 `LICENSE.txt`。
