# M5Stack PaperColor 设备状态看板

[English](README.md)

这是一个面向 **M5Stack PaperColor** 的独立设备状态看板项目，使用 PlatformIO 构建，在 4 英寸彩色电子墨水屏上展示设备、供电、存储、传感器、时钟和网络等关键信息。

![M5Stack PaperColor 硬件概览](docs/images/m5stack-papercolor-overview.png)

## 关于 M5Stack PaperColor

M5Stack PaperColor 是一款基于乐鑫 ESP32-S3R8 的便携式彩色电子墨水设备。它配备低功耗 4 英寸显示屏及多种板载外设，适合开发信息看板和电池供电的嵌入式项目。

| 组件 | 规格 |
| --- | --- |
| 主控 | ESP32-S3R8，16 MB Flash、8 MB PSRAM |
| 显示屏 | 4.0 英寸 ED2208 电子墨水屏，400 × 600，Spectra 6 全彩 |
| 存储 | microSD 卡槽 |
| 传感器 / 电源管理 | SHT40、RX8130CE RTC、M5PM1 |
| 音频 | ES8311 编解码器、MEMS 麦克风、1 W 扬声器 |
| 供电 / 输入 | 1,250 mAh 电池、USB-C、A/B/C 按键和电源键 |

完整硬件资料和引脚信息请查看 [M5Stack PaperColor 官方文档](https://docs.m5stack.com/zh_CN/core/PaperColor)。

## 功能

- 设备概览：型号、ESP32-S3 资源、SDK、CPU 频率、堆内存和 PSRAM。
- 电源信息：电池电量与电压、USB 连接和充电状态。
- 存储信息：microSD 总量、已用/剩余空间，以及受限深度的文件扫描。
- 环境信息：SHT40 温度和相对湿度。
- 时钟与网络信息：RX8130 RTC 时间、Wi-Fi MAC 地址和唤醒次数。
- 针对 PaperColor 400 × 600 电子墨水屏设计；开机刷新，之后每五分钟刷新一次。
- 按键 A/B 请求刷新；按键 C 进入安全关机流程。
- RTC 时间失效时，每个新烧录固件仅使用编译时间校正一次。

## 项目结构

```text
m5papercolor-projects1/
├── src/                 # 固件源码
├── tests/               # 源码级回归测试
├── docs/images/         # README 图片
├── platformio.ini       # PlatformIO 环境和依赖
├── build_time.py        # 注入固件编译时间
├── requirements.txt     # 开发工具依赖
├── README.md            # 英文说明
└── README.zh-CN.md      # 中文说明
```

## 硬件说明

- 项目在 M5Unified 中显式选择 `M5PaperColor` 板型；PlatformIO 的通用开发板配置仅用于提供 ESP32-S3 构建目标。
- microSD 使用 PaperColor 的 SPI 引脚：SCK GPIO 15、MISO GPIO 14、MOSI GPIO 13、CS GPIO 47。
- 电子墨水屏不应像 LCD 一样频繁重绘，因此常规刷新周期刻意设置得较长。

## 开始使用

```bash
python3 -m pip install -r requirements.txt
python3 -m unittest discover -s tests -v
pio run
pio run -t upload --upload-port /dev/cu.usbmodem21201
```

请将串口名称替换为实际设备端口。

## 许可

本项目用于个人和学习用途。重新发布二进制文件前，请确认第三方依赖的许可证要求。
