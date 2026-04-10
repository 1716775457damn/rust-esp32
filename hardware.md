# 硬件配置文档

## 主控芯片

- **芯片型号**：ESP32-S3
- **开发框架**：ESP-IDF 5.4+
- **编程语言**：C++

---

## 显示屏

| 参数 | 值 |
|------|-----|
| 型号 | EPI1831T |
| 尺寸 | 1.8 寸圆屏 |
| 分辨率 | 360 × 360 |
| 接口 | QSPI（四线 SPI） |
| 驱动芯片 | ST77916 |
| 触摸 | 无 |

> 注：ST77916 使用 QSPI 接口（quad_mode = 1），lcd_cmd_bits = 32，初始化时需先以低速（3MHz）读取寄存器 0x04 判断版本，再切换到高速（80MHz）正式通信。

---

## 音频

### 功放控制
| 参数 | 值 |
|------|-----|
| 功放使能引脚 | GPIO11 |
| 使能电平 | 高电平（1）= 失能，低电平（0）= 使能 |
| 备注 | GPIO11 通过 `esp_efuse_write_field_bit(ESP_EFUSE_VDD_SPI_AS_GPIO)` 复用为普通 GPIO |

### 扬声器 I2S（MAX98357A）
| 信号 | GPIO |
|------|------|
| BCLK | GPIO5 |
| LRCLK | GPIO4 |
| DATA | GPIO6 |
| SD_MODE（使能） | GPIO45 |

### 麦克风 I2S（MSM261）
| 信号 | GPIO |
|------|------|
| BCLK | GPIO7 |
| WS | GPIO9 |
| DATA | GPIO8 |

---

## UART 外设

### UART1 — YT2228 蓝牙/语音模块
| 参数 | 值 |
|------|-----|
| 端口 | UART_NUM_1 |
| RX | GPIO13 |
| TX | GPIO12 |
| 缓冲区 | 512 字节 |

支持指令枚举：
- 唤醒小智、进入配网、唤醒词模式/结束/中止
- 蓝牙模式开关、蓝牙音乐播放控制（暂停/继续/上一首/下一首）
- 音量增减/最大/最小
- 蓝牙已连接/断开

### UART0 — PFS123 低电量检测
| 参数 | 值 |
|------|-----|
| 端口 | UART_NUM_0 |
| RX | GPIO9 |
| 缓冲区 | 512 字节 |

---

## SPI 显示引脚（EPI1831T ST77916 QSPI）

| 信号 | GPIO |
|------|------|
| DATA0（MOSI） | GPIO17 |
| DATA1 | GPIO15 |
| DATA2 | GPIO16 |
| DATA3 | GPIO13 |
| SCLK | GPIO18 |
| CS | GPIO12 |
| RST | 无（-1，通过 TCA9554 IO 扩展器复位） |
| 背光（BL） | GPIO18 |

> 注：待确认实际接线后更新此表。ST77916 QSPI 模式下 CS 引脚必须独立，不能与其他 SPI 设备共用。

## 按钮

| 按钮 | GPIO |
|------|------|
| BOOT 按钮 | GPIO0 |

---

## LED

| 类型 | GPIO |
|------|------|
| APA102 DATA | GPIO38 |
| APA102 CLOCK | GPIO39 |

---

## 旋转编码器（Knob）

| 信号 | GPIO |
|------|------|
| DATA_A | GPIO47 |
| DATA_B | GPIO48 |

---

## 音频采样率

| 参数 | 值 |
|------|-----|
| 输入采样率 | 24000 Hz |
| 输出采样率 | 24000 Hz |
| 音频编解码 | OPUS（60ms 帧） |

---

## 分区表（参考）

```
nvs,      data, nvs,     0x9000,   0x5000
phy_init, data, phy,     0xF000,   0x1000
factory,  app,  factory, 0x10000,  0x200000   # 2MB 固件
spiffs,   data, spiffs,  0x210000, 0x1F0000   # ~2MB 图片存储
```

---

## 备注

- GPIO11 需通过 eFuse 将 VDD_SPI 复用为普通 GPIO，**烧录一次不可逆**，操作前确认硬件支持
- EPI1831T 为圆形屏，LVGL 显示时建议配合圆形裁剪（`lv_obj_set_style_clip_corner`）避免四角显示异常
- 无触摸屏，交互依赖 BOOT 按钮（GPIO0）或旋转编码器（GPIO47/48）
- ST77916 驱动需使用 QSPI 模式（`quad_mode = 1`），`lcd_cmd_bits = 32`，`lcd_param_bits = 8`
- ST77916 初始化须先以 3MHz 低速读取寄存器 0x04 判断屏幕版本，再切换到 80MHz 高速模式
- **Rust 编译路径限制**：`esp-idf-sys` 要求项目路径 ≤ 10 字符，Windows 下必须在 `C:\r` 等极短路径下编译 Rust 组件，`subst` 命令无效
- **Rust 环境变量**：每次编译前需执行 `& '%USERPROFILE%\export-esp.ps1'` 设置 `LIBCLANG_PATH` 和 `PATH`
- **源码同步**：修改 `tarot-esp32/components/tarot_core/` 下的 Rust 源码后，需 `xcopy` 同步到 `C:\r` 再执行 `cargo +esp check --target xtensa-esp32s3-espidf`
