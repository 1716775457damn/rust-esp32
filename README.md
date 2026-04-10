# 🔮 Tarot ESP32: 灵动塔罗抽卡机

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.3.1-blue)](https://github.com/espressif/esp-idf)
[![Rust](https://img.shields.io/badge/Rust-1.75+-orange)](https://www.rust-lang.org/)
[![Status](https://img.shields.io/badge/Phase-3_Complete-green)](#)

这是一个基于 **ESP32-C3** 开发的智能塔罗抽卡机。它结合了 Rust 语言的高性能、内存安全性与 ESP-IDF 的强大驱动能力，支持通过网页无线上传塔罗卡面，并实现了高效的流式图像解码。

---

## ✨ 核心特性

- **🚀 流式图像解码**：独创的 16 行缓冲区（Row-Buffered）JPEG 解码技术，在 ESP32-C3 有限的内存（~150KB）下完美处理 360x360 RGB565 高清图像。
- **📟 无线上传**：内置 Wi-Fi AP 模式与 HTTP 服务器，支持手机/电脑通过浏览器直接上传图片。
- **💾 16MB 存储优化**：充分利用 16MB 外部 Flash，配置了 10MB 大容量 SPIFFS 分区，可存储超过 30 张高分辨率塔罗卡面。
- **🛡️ 混合开发架构**：
  - **Rust Core**: 负责网络、图像处理、业务逻辑。
  - **C++ Shim**: 负责硬件抽象层与引导。

---

## 🛠️ 进度追踪 (Milestones)

- [x] **Phase 1**: 环境搭建与 Rust/C++ 混合编译链路通畅。
- [x] **Phase 2**: Wi-Fi AP 与 HTTP 基础服务器搭建。
- [x] **Phase 3**: 高性能 JPEG 至 RGB565 流式转换引擎。
- [ ] **Phase 4**: LVGL 屏幕驱动集成与翻牌特效实现。
- [ ] **Phase 5**: 抽卡逻辑算法与 UI 美化。

---

## 🔧 硬件需求

- **MCU**: ESP32-C3 (建议 16MB Flash 版本)
- **屏幕**: 待接入 (推荐 ST7789 / GC9A01)
- **其他**: 锂电池管理模块、3D 打印外壳

---

## 🚀 快速开始

### 1. 编译环境
确保已安装 `esp-idf v5.3.1` 和 `rust-esp` 工具链。

### 2. 编译与烧录
```powershell
cd tarot-esp32
idf.py build
idf.py -p COM4 flash monitor
```

### 3. 上传图片
1. 手机连接 Wi-Fi: `TarotCard`。
2. 浏览器打开 `192.168.71.1`。
3. 选择一张正方形图片并上传。

---

## 👨‍💻 贡献说明
代码采用了模块化设计，核心逻辑位于 `components/tarot_core`。

---

*“命运的齿轮已开始转动。”*