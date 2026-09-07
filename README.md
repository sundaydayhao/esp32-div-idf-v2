# ESP32-DIV IDF v2

ESP32-DIV 的 C / ESP-IDF 工程，使用 ESP-IDF 5.5.x（本机为 v5.5.5），面向 ESP32-S3 的 V3、V4 板型，提供无线工具、显示界面、存储管理和设备配置能力。

## 项目结构

- `main/`：应用入口、功能模块和平台抽象实现
- `components/`：触摸屏、I/O 扩展器等外设组件
- `managed_components/`：通过 ESP-IDF Component Manager 管理的依赖
- `hardware/`：硬件资料
- `docs/`：设计文档和说明
- `assets/`：图片与静态资源

## 构建与烧录

构建脚本默认使用 `C:\esp\v5.5.5\esp-idf`，优先加载本机 EIM 安装的 v5.5.5 PowerShell 环境。手动打开环境的正确命令是：

```powershell
powershell.exe -NoExit -ExecutionPolicy Bypass -NoProfile -Command "& { . 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1' }"
```

在项目目录分别编译两种板型：

```powershell
./tools/build.ps1 -Board v3
./tools/build.ps1 -Board v4
```

V3（ILI9341，240×320）的固件位于 `build/idf55-v3/esp32_div.bin`，V4（ST7796，320×480）位于 `build/idf55-v4/esp32_div.bin`。两个目录各自保存 `sdkconfig`，避免板型和旧 IDF 6.0 构建缓存混用。现有根目录配置保持 V4；自定义参数应复制到相同板型的新构建目录。旧构建产物仍可保留。

VS Code 的 ESP-IDF 扩展也已选择 v5.5.5 和 V4 的新构建目录。使用手动 IDF 命令时明确传入目录，例如 `idf.py -B build/idf55-v4 build`，避免使用根 `build/` 下的旧缓存。

烧录时明确指定板型及实际串口，例如当前 V4：

```powershell
./tools/build.ps1 -Board v4 -Flash -Port COM16
```

V3 使用 `-Board v3` 和它的实际端口。脚本检查 SDK 版本、硬件目标、全部应用源文件为 C 以及 OTA 分区容量后再烧录。JSON 使用 IDF 5.5 自带的 `json` 组件。

### C 显示界面

主菜单恢复原版的两列四行布局，保留 More 网格、分类菜单顺序、WiFi/Bluetooth 分页、顶部状态栏及原版 Font 1/Font 2 点阵字体和图标。字体和图标已经转换为 C 常量，不依赖 Arduino 或 TFT_eSPI。Settings 和 About 从主菜单直接进入。

V3 的 ILI9341 和 V4 的 ST7796 原生扫描方向均采用原版 TFT_eSPI 的 `MX | BGR`。`div_board.h` 中的 `DIV_LCD_NATIVE_MIRROR_X` 传入 LVGL 显示配置，以免 LVGL 更新旋转时清掉 MX，导致文字左右反写。

GPIO14/21 保持第三个 NRF24 优先，GPIO2 保持蜂鸣器优先。未启用电池采样时，状态栏显示 `--%`。AUX 返回上一层并停止当前任务。

未校准的设备启动后会进入触摸校准：依次按住左上、右上、右下、左下、中心的十字约半秒并松开。中心验证通过后保存到 NVS，重启无需重复。旧固件的默认坐标不会被当作实物校准结果。也可进入 Tools → Touch Calibrate，或长按 AUX 两秒重新校准；短按 AUX 取消。校准前可使用物理按键操作，触摸点击暂不启用，避免点错菜单。

`./tools/test-ui.ps1` 在电脑上运行与固件相同的 C/LVGL 菜单代码，检查网格坐标、分页、按键导航和菜单事件，并将画面输出到 `build/ui-preview/*.ppm`。`tools/import-ui-assets.py` 可从原项目重新生成字库和图标，普通编译无需原项目。

WiFi → Packet Monitor 已恢复上方实时曲线、下方彩色瀑布图。进入后持续监听信道 1，顶部 `− / +` 或物理左右键切换信道；底部 Stop 停止、Run 重新开始、Config 设置时长/跳频/PCAP、Back 返回。图表与原版一样，对接收数据包的时间变化做 FFT；它不是射频采样仪的频谱。普通 WiFi Scanner 仍显示 AP 列表。界面测试还验证 FFT、采样窗口、静默时段、瀑布历史和内存释放。

当前恢复范围包括菜单、公共界面及 Packet Monitor 图表；其他功能执行页仍使用 C 服务层的参数与日志界面，尚未逐页复刻 Arduino 原版的专用图表和交互。

V3 瀑布图保持原有 128 级颜色；V4 大屏采用 4 位索引缓存（15 级数据颜色及分隔线），保留全屏像素尺寸和采样精度，在相同的 96 KiB LVGL 内存池中运行。画面测试覆盖 V3/V4 的横屏、竖屏、历史更新和缓存释放。

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

项目以主板 + 扩展板的方式组织，具体板型由 `components/div_board/include/div_board.h` 和该组件的 Kconfig 选择。

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
