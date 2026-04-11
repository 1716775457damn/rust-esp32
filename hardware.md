# 🔮 Tarot ESP32 硬件规格书 (Xiaozhi PANBOPO 版)

本规格书已根据 `F:\xiaozhi-esp32-1.8.8` (小智面板项目) 的 `PANBOPO` 开发板配置文件进行了完整同步。

---

## 1. 核心 MCU
- **芯片**: ESP32-C3 (RISC-V)
- **Flash**: 16MB
- **PSRAM**: 无 (依靠 400KB 内置 RAM)

---

## 2. 显示系统 (ST77916 圆屏)
| 信号 | GPIO | 说明 |
|------|------|------|
| **SPI SCLK** | **GPIO 1** | 时钟线 |
| **SPI MOSI** | **GPIO 2** | 数据线 (Standard SPI) |
| **LCD DC** | **GPIO 0** | 命令/数据切换 |
| **LCD CS** | **GPIO 21** | 片选 |
| **LCD BL** | **GPIO 20** | 背光控制 (PWM) |
| **LCD RST** | **-** | 无直接引脚，使用软件复位指令 (0x01) |

- **面板特性**: 360x360 圆形屏。
- **驱动模式**: Standard SPI, 8-bit CMD/Param。
- **色彩模式**: RGB565 (Inverted)。

---

## 3. 音频系统 (MAX98357A & Mic)
| 信号 | GPIO | 说明 |
|------|------|------|
| **I2S BCLK** | **GPIO 8** | 位时钟 |
| **I2S WS (LRC)** | **GPIO 6** | 左右声道切换 |
| **I2S DOUT** | **GPIO 5** | 音频输出 (连接 MAX98357A DIN) |
| **I2S DIN** | **GPIO 7** | 音频输入 (连接麦克风 DATA) |
| **I2S MCLK** | **GPIO 10** | 主时钟 (可选) |

- **功放使能**: GPIO 11 (需 eFuse 复用，默认建议拉低)。

---

## 4. 控制与通信
| 信号 | GPIO | 说明 |
|------|------|------|
| **I2C SDA** | **GPIO 3** | 传感器/扩展 IO 数据线 |
| **I2C SCL** | **GPIO 4** | 传感器/扩展 IO 时钟线 |
| **BOOT 按钮** | **GPIO 9** | 兼做功能按键 |
| **UART TX** | **GPIO 13** | UART1 (蓝牙/外部通信) |
| **UART RX** | **GPIO 12** | UART1 (蓝牙/外部通信) |

---

## 5. 软件适配说明
- **SPI 频率**: 推荐 `15MHz` (稳定画质) 或 `40MHz` (极限速度)。
- **I2S 采样率**: 推荐 `24000Hz` 或 `16000Hz` (16-bit Mono)。
- **I2C 速率**: 标准 `100KHz` 或 `400KHz`。

---
*注：文档内容已校对至小智 1.8.8 源码库。关键引脚若有变动，请优先参考此文档。*
