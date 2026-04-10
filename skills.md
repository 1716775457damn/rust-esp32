# 项目技能清单（Skills）

## 硬件平台
- ESP32-S3 芯片开发（含 PSRAM/Flash 使用）
- ESP-IDF 5.4+ 框架配置与构建（CMake + Kconfig）
- SPI 接口 LCD 驱动适配（EPI1831T，360×360 圆屏，无触摸）
- GPIO 控制（功放使能 GPIO11，eFuse VDD_SPI 复用）
- UART 外设通信（YT2228 蓝牙模块 UART1，PFS123 低电量检测 UART0）
- I2S 音频接口（MAX98357A 扬声器，MSM261 麦克风）
- APA102 LED 控制（SPI 协议）
- 旋转编码器输入（GPIO47/48）
- BOOT 按钮交互（GPIO0，无触摸屏替代输入）

## 显示与 UI
- LVGL 8.x UI 框架（对象模型、样式、布局）
- SpiLcdDisplay 驱动封装与 Board 抽象接口实现
- 圆形屏适配（`lv_obj_set_style_clip_corner` 裁剪四角）
- LVGL 动画系统（`lv_anim_t`，缓动曲线，`lv_anim_set_path_cb`）
- 伪 3D 翻转动画（宽度缩放模拟翻牌，卡背/卡面切换）
- 洗牌散开动画（多对象位移 + 旋转，`lv_img_set_angle`）
- 半透明遮罩 + 文字叠加（底部卡名显示）
- 呼吸灯动画（提示文字透明度循环）
- `lv_img_dsc_t` 构造（从 SPIFFS 加载 RGB565 数据直接渲染）
- PWM 背光控制（`PwmBacklight`）
- PowerSaveTimer 省电模式集成

## Rust 嵌入式开发
- Xtensa Rust 工具链配置（`espup` 安装，`esp-idf-sys` 绑定）
- `esp-idf-hal` 硬件抽象层使用
- `esp-idf-svc` 服务层（Wi-Fi AP 模式、HTTP Server、SPIFFS 文件系统）
- Wi-Fi AP 模式配置（SSID/密码/固定 IP 192.168.4.1）
- `EspHttpServer` 路由注册（GET `/`，POST `/upload`）
- multipart/form-data 解析（图片上传接收）
- JPEG 解码（`jpeg-decoder` crate，输出 RGB888 转 RGB565）
- PSRAM 堆内存分配（`heap_caps_malloc` via `esp-idf-sys`，`MALLOC_CAP_SPIRAM`）
- SPIFFS 文件读写（`card_N.rgb` 持久化存储）
- NVS 键值存储（图片元数据：宽高、槽位状态）
- `cbindgen` 生成 C 头文件（`build.rs` 自动化）
- Rust 静态库编译（`crate-type = ["staticlib"]`）

## C++ / Rust FFI 混合架构
- `extern "C"` 接口设计（Rust 导出，C++ 调用）
- FFI 安全边界管理（裸指针传递、内存所有权约定）
- CMake 集成 Rust 静态库（`add_prebuilt_library` 或 `cargo` 自定义命令）
- C++ 胶水层封装（`rust_bridge.cc/h`）

## 卡牌逻辑
- 无重复随机抽卡算法（Fisher-Yates shuffle 或 drawn_history 过滤）
- 22 张大阿卡纳牌组管理
- 本轮抽卡历史追踪与重置
- 卡槽状态查询（已上传/未上传）

## 网页前端（内嵌固件）
- 纯 HTML + CSS + JS（无框架，压缩 < 20KB）
- Canvas API 客户端图片预缩放（目标尺寸 360×360，适配圆屏）
- `fetch` API 发送 multipart/form-data
- 22 槽位网格布局（已上传缩略图 / 占位符）
- 上传进度条与状态反馈
- 响应式布局（手机竖屏友好）
- `include_str!` 编译进 Rust 固件

## 存储与内存管理
- SPIFFS 分区规划（≥ 2MB，22 张卡 × ~75KB/张）
- 分区表定制（`partitions.csv`）
- 解码即写盘策略（PSRAM 临时缓冲，写入后立即释放）
- 重启数据完整性保障（NVS + SPIFFS 双重持久化）

## 测试与调试
- 全流程集成测试（上传 → 存储 → 抽卡 → 显示）
- PSRAM 内存泄漏检测（连续抽卡压力测试）
- LVGL 帧率优化（`timer_period_ms` 调整，目标 ≥ 20fps）
- Wi-Fi AP 多设备并发稳定性测试
- ESP-IDF 串口日志调试（`ESP_LOGI/LOGE`）
