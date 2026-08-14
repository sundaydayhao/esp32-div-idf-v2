# ESP32-DIV 功能设计文档

## 1. 项目定位

ESP32-DIV 是一个基于 ESP-IDF 的 ESP32-DIV 工程，面向 ESP32-S3 平台，也兼容部分历史板型。工程入口为 `main/app.cpp`，显示、存储、SD 卡和平台抽象分别由 `DivDisplay`、`DivNvStore`、`DivSd` 等模块提供。

当前板型通过 `main/board_config.h` 和 Kconfig 进行选择，默认板型为 `BOARD_ESP32_DIV_V3`。可选板型包括：

- `BOARD_CYD`
- `BOARD_ESP32_DIV_V1`
- `BOARD_ESP32_DIV_V2`
- `BOARD_ESP32_DIV_V3`
- `BOARD_ESP32_DIV_V4`

## 2. 架构分层

| 层级 | 目录 | 职责 |
| --- | --- | --- |
| 入口层 | `main/` | 初始化 ESP-IDF 运行环境并进入应用主循环 |
| 应用层 | `main/` | 菜单、UI、Wi-Fi、BLE、Sub-GHz、IR、RFID/NFC、GPS、设置和文件管理 |
| 平台层 | `main/include/` 与 `main/src/` | 显示、串口、SPI、SD/VFS、Wi-Fi、NVS、TFT、系统计时和 GPIO 封装 |
| 板级层 | `main/board_config.h`、`main/board_pins.h`、`main/tft_setup.h` | 板型、屏幕、引脚和默认参数 |
| 外设组件 | `components/` | 触摸屏、PCF8574、第三方兼容组件 |
| 托管组件 | `managed_components/` | 通过 IDF Component Manager 下载的依赖 |

平台层的目标是让业务代码只依赖项目内部 `Div*` 接口，并将底层驱动差异集中到统一封装中。

## 3. 依赖策略

本项目优先使用 ESP-IDF 内置能力，必要时再引入托管组件。

### 托管组件

| 组件 | 用途 |
| --- | --- |
| `espressif/cjson` | JSON 解析与配置文件读写 |
| `espressif/mdns` | 设备发现与网络服务辅助 |
| `espressif/esp-dsp` | 频谱、FFT 和信号处理 |

### 内置能力

其余功能优先使用 ESP-IDF 原生模块，包括：

- `esp_wifi`
- `esp_event`
- `esp_netif`
- `nvs_flash`
- `fatfs`
- `sdmmc`
- `esp_http_server`
- `esp_https_ota`
- `app_update`
- `esp_driver_*`
- FreeRTOS
- VFS

## 4. 板级与显示

板型由 `main/board_config.h` 和 Kconfig 共同决定，同一构建只能启用一个 `BOARD_*` 宏。

显示面板由 `TFT_PANEL_ILI9341` 或 `TFT_PANEL_ST7796` 选择。当前工程中：

- `BOARD_ESP32_DIV_V4` 默认使用 `ST7796 / ST7796S`
- 其余板型默认使用 `ILI9341`

`main/tft_setup.h` 保存显示相关参数，包括：

- SPI 引脚：`TFT_MISO`、`TFT_MOSI`、`TFT_SCLK`、`TFT_CS`、`TFT_DC`、`TFT_RST`、`TFT_BL`
- 屏幕分辨率：ILI9341 为 `240x320`，ST7796 为 `320x480`
- SPI 频率：默认 `27 MHz`
- 背光逻辑：`TFT_BACKLIGHT_ON`
- 颜色顺序、反色模式和面板驱动配置

显示抽象由 `DivDisplay` 和 `DivSprite` 提供，底层通过 ESP-IDF SPI master 驱动发送图像数据。

## 5. 数据存储

| 数据 | 路径 | 说明 |
| --- | --- | --- |
| 应用设置 | `/config/settings.json` + NVS | 设置文件优先，NVS 作为关键小数据缓存 |
| 触摸校准 | `/config/settings.json` | 按 `TOUCH_PROFILE_ID` 保存板型校准参数 |
| Captive Portal 记录 | `/captive_portal/captured.csv` + NVS | SD 保存日志，NVS 保存索引与状态 |
| Sub-GHz 配置 | `/subghz/profiles_current.bin`、`/subghz/profiles_*.bin` | 当前配置与导出备份 |
| 红外配置 | `/ir/*.ir` | 保存载波频率、原始波形和名称 |
| BLE Ducky 脚本 | `/ducky/*.duck`、`/ducky/*.txt` | 从 SD 读取并通过 BLE HID 执行 |
| Wardriver 日志 | `/wd_*.csv`、`/wardrive/*.csv`、`/wd_wigle_upload.csv` | GNSS、Wi-Fi 和 BLE 观测记录 |
| 固件文件 | `/firmware.bin` | SD 升级输入文件 |

SD 文件访问通过 `DivSd` 和 `DivFile` 映射到 VFS/FATFS。持久化键值通过 `DivNvStore` 映射到 NVS。

## 6. 功能模块

| 模块 | 源文件 | 主要能力 |
| --- | --- | --- |
| 主菜单和导航 | `main/src/app.cpp`、`main/src/utils.cpp`、`main/src/touchscreen.cpp`、`main/src/keyboard_ui.cpp` | 启动、菜单、状态栏、触摸导航、键盘输入和通知弹窗 |
| Wi-Fi | `main/src/wifi.cpp` | 扫描、监控、PCAP、Beacon、Deauth、Portal、Probe、固件上传 |
| BLE / 2.4GHz | `main/src/bluetooth.cpp`、`main/src/ducky.cpp` | BLE 扫描、广播、HID、嗅探和 2.4GHz 相关功能 |
| Sub-GHz | `main/src/subghz.cpp` | CC1101 接收、重放、干扰与 profile 管理 |
| IR | `main/src/ir.cpp` | 红外采集、重放、遥控器配置和 profile 管理 |
| RFID/NFC | `main/src/rfid.cpp` | PN532 读卡、克隆、dump、擦除、干扰和解码 |
| GPS | `main/src/gps.cpp` | GNSS 读取、卫星视图、Wardriver 和 WiGLE CSV |
| DSP | `main/src/signal_dsp.cpp` | 频谱窗口、FFT 和图形数据处理 |

## 7. 无线与任务模型

Wi-Fi 由 `esp_wifi` 管理。后台扫描任务只在主界面空闲时运行，前台功能运行时会暂停后台任务，避免射频资源冲突。

BLE 使用 IDF NimBLE host。广播、扫描和 HID 服务按需启停，避免长期占用资源。

长时间任务采用 FreeRTOS task 或循环分片执行。所有功能在退出时都需要响应 `feature_exit_requested` 和返回键事件。

## 8. 固件更新

SD 更新流程读取 `/firmware.bin`，并通过 ESP-IDF app update API 写入 OTA 分区，成功后自动重启。

Web 更新通过 `esp_http_server` 提供上传接口，上传完成后使用同一套 app update 流程写入固件。

## 9. UI 规则

- 功能页使用 `featureClearContent()` 清理内容区域，保留底部触摸导航栏
- 状态栏通过 `drawStatusBar()` 更新，部分场景会强制刷新
- 触摸校准按板型保存，不能跨板共享
- 显示尺寸和布局必须通过 `tft.width()`、`tft.height()` 或板级宏读取
- 新 UI 控件应尽量复用 `FeatureUI`、`KeyboardUI` 和现有颜色常量

## 10. 维护规则

1. 新代码只能依赖 ESP-IDF、托管组件或项目内 `Div*` 平台接口。
2. 板级引脚只允许在 `board_config.h`、`board_pins.h`、`tft_setup.h` 和 `shared.h` 中维护。
3. 文件系统路径必须遵守本文第 5 节的约定，新增路径时同步更新文档。
4. 功能模块不得长期独占共享射频或 SPI 资源，退出时必须恢复总线状态。
5. 构建验证以 `idf.py build` 为准，默认目标为 ESP32-S3。

## 11. 编码说明

原始文档中存在编码损坏问题，已统一改写为 UTF-8 中文版，并按当前源码结构重新整理。后续如果板型、路径或模块名称有变化，应同步更新此文档。
