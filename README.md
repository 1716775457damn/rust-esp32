开发计划：ESP32-S3 离线塔罗抽卡机
一、项目定位与技术边界
硬件基础（与现有项目相同）：

ESP32-S3 芯片（含 PSRAM/Flash）

LCD 屏幕（SPI 接口，LVGL 驱动）

GPIO11 功放控制

UART0（GPIO9 RX，PFS123 低电量检测）

UART1（GPIO12/13，YT2228 蓝牙/语音模块）

Rust 使用策略（关键决策）：

ESP-IDF 官方支持 Rust（esp-idf-hal + esp-idf-svc），但 LVGL 绑定（lvgl-rs）在 ESP32 上不成熟。因此采用混合架构：

模块	语言	理由
AP 网络 + HTTP 服务器	Rust	esp-idf-svc 原生支持，最适合 Rust
图片接收/解码/存储	Rust	内存安全优势明显
卡牌数据管理	Rust	纯逻辑，Rust 最擅长
LVGL UI + 动画	C++	LVGL 本身是 C 库，C++ 绑定最稳定
硬件初始化（GPIO/UART）	C++	复用现有代码
Rust ↔ C++ 接口	FFI	extern "C" 桥接
二、项目结构设计
tarot-esp32/
├── CMakeLists.txt                  # 顶层构建，集成 Rust 组件
├── sdkconfig.defaults.esp32s3
├── main/
│   ├── CMakeLists.txt
│   ├── main.cc                     # 入口，初始化各模块
│   ├── display/
│   │   ├── tarot_display.cc/h      # LVGL 塔罗牌 UI
│   │   └── card_animation.cc/h     # 抽卡动画状态机
│   ├── boards/
│   │   └── tarot-board/            # 复用现有板级配置
│   └── ffi/
│       ├── rust_bridge.h           # C++ 调用 Rust 的接口声明
│       └── rust_bridge.cc          # FFI 胶水代码
└── components/
    └── tarot_core/                 # Rust 组件（Cargo workspace）
        ├── Cargo.toml
        ├── build.rs                # 生成 C 头文件（cbindgen）
        └── src/
            ├── lib.rs              # FFI 导出入口
            ├── ap_server.rs        # Wi-Fi AP + HTTP 服务器
            ├── image_handler.rs    # 图片接收、JPEG 解码、NVS 存储
            └── card_manager.rs     # 卡牌数据管理、随机抽取逻辑

Copy
三、分阶段开发计划
Phase 1：环境搭建与骨架验证（约 3 天）
目标： 确认 Rust + C++ 混合编译在 ESP32-S3 上可行

任务：

基于现有项目裁剪，删除 AI/音频/协议相关代码，保留 LCD 显示、LVGL、GPIO 初始化

配置 esp-idf-sys + esp-idf-hal 的 Rust 工具链（espup 安装 Xtensa Rust 工具链）

创建 components/tarot_core Cargo 项目，编写一个最小 FFI 函数（如返回版本字符串），验证 C++ 能调用 Rust

验证 LVGL 在裁剪后的项目中正常显示一个纯色屏幕

产出： 可编译运行的骨架项目，C++ 成功调用 Rust FFI

Phase 2：AP 网络 + HTTP 服务器（Rust，约 4 天）
目标： ESP32-S3 开热点，用户连接后访问固定 IP 的网页

技术方案：

使用 esp-idf-svc::wifi::EspWifi 配置 AP 模式（SSID: TarotCard，IP: 192.168.4.1）

使用 esp-idf-svc::http::server::EspHttpServer 提供 HTTP 服务

路由设计：

GET / → 返回内嵌的 HTML 上传页面（include_str! 编译进固件）

POST /upload → 接收 multipart 图片数据，存入 NVS 或 SPIFFS

关键 Rust 代码结构：

// ap_server.rs
pub fn start_ap_and_server(card_slots: usize) -> anyhow::Result<()>
// 启动 AP，绑定 HTTP server，注册路由
// 上传完成后通过回调通知 C++ 侧刷新显示

Copy
rust
FFI 接口（暴露给 C++）：

// rust_bridge.h
void rust_start_ap_server(void);
bool rust_get_uploaded_image(uint8_t slot, uint8_t** data, size_t* len);
void rust_free_image(uint8_t* data);

Copy
c
产出： 手机连接热点，打开 192.168.4.1，能看到上传页面并成功上传图片

Phase 3：图片处理与存储（Rust，约 3 天）
目标： 接收 JPEG/PNG，解码为 RGB565，存入 NVS，供 LVGL 显示

技术方案：

使用 tinyjpeg 或 jpeg-decoder crate 解码 JPEG（注意 ESP32-S3 内存限制，图片建议压缩到 360×360 以内）

存储方案：使用 ESP-IDF 的 SPIFFS（通过 esp-idf-svc::fs），每张卡存一个文件 card_0.rgb

支持最多 22 张卡（大阿卡纳）或用户自定义数量（由 Kconfig 配置）

图片元数据（宽高、槽位）存入 NVS

内存策略：

解码时使用 PSRAM（MALLOC_CAP_SPIRAM），通过 esp-idf-sys 的 heap_caps_malloc 分配

解码完成后立即写入 SPIFFS，释放 PSRAM

产出： 上传的图片能正确解码并持久化存储，重启后不丢失

Phase 4：卡牌管理逻辑（Rust，约 2 天）
目标： 实现抽卡逻辑，管理卡牌状态

技术方案：

// card_manager.rs
pub struct CardManager {
    total_cards: usize,
    drawn_history: Vec<u8>,  // 已抽过的卡（本轮）
}

impl CardManager {
    pub fn draw_card(&mut self) -> Option<u8>  // 返回卡槽 index，不重复
    pub fn reset_deck(&mut self)               // 重置一轮
    pub fn get_card_image(&self, slot: u8) -> Option<&[u8]>  // 返回 RGB565 数据
    pub fn card_count(&self) -> usize
}

Copy
rust
FFI 接口：

uint8_t rust_draw_card(void);          // 返回卡槽 index，0xFF 表示无牌
void    rust_reset_deck(void);
bool    rust_load_card_image(uint8_t slot, uint8_t** data, uint32_t* width, uint32_t* height);

Copy
c
产出： 抽卡逻辑正确，不重复，支持重置

Phase 5：LVGL 塔罗牌 UI + 抽卡动画（C++，约 5 天）
目标： 实现完整的塔罗牌视觉体验

UI 状态机设计：

[待机界面] --触摸/按钮--> [洗牌动画] --> [抽卡翻转动画] --> [展示界面] --返回--> [待机界面]
                                                                    |
                                                              [无牌提示] --> [重置]

Copy
各界面设计：

待机界面：

黑色背景，屏幕中央显示一叠牌背（用矩形+渐变模拟叠层感）

底部提示文字"触摸抽卡"（循环呼吸动画）

右上角显示剩余牌数

洗牌动画（约 1.5 秒）：

用 LVGL lv_anim_t 实现：多张卡背图片从中心向四周散开再收回

使用 lv_anim_set_path_cb 设置缓动曲线（ease-in-out）

散开时轻微旋转（lv_img_set_angle）增加随机感

抽卡翻转动画（约 1 秒）：

模拟 3D 翻转：用 LVGL 的 lv_anim_t 控制图片宽度从 W→0（正面→侧面），再从 0→W（侧面→背面）

宽度缩小阶段显示卡背，宽度放大阶段切换为卡面图片

配合 lv_obj_set_style_transform_pivot_x 设置翻转中心

展示界面：

卡面图片居中全屏显示（从 SPIFFS 加载 RGB565 数据，构造 lv_img_dsc_t）

卡名/编号显示在底部半透明遮罩上

长按 2 秒返回待机

产出： 完整流畅的抽卡视觉体验

Phase 6：网页上传界面（HTML/JS，约 2 天）
目标： 简洁好用的卡牌上传网页，内嵌进固件

网页功能：

显示 22 个卡槽（网格布局），已上传的显示缩略图，未上传的显示占位符

点击槽位 → 弹出文件选择 → 预览 → 确认上传

上传进度条

"全部清除"按钮

响应式设计，手机竖屏友好

技术：

纯 HTML + CSS + JS，无框架依赖，压缩后 < 20KB

使用 fetch API 发送 multipart/form-data

图片在客户端预先缩放到 360×360（用 Canvas API），减少传输量和解码压力

产出： 网页内嵌进 Rust 代码（include_str!），无需外部文件

Phase 7：集成测试与优化（约 3 天）
任务：

全流程测试：上传图片 → 存储 → 抽卡 → 显示

内存压力测试：连续抽卡 100 次，检查 PSRAM 泄漏

动画帧率优化：目标 ≥ 20fps（LVGL timer_period_ms 调整）

断电重启后卡牌数据完整性验证

多设备同时连接 AP 的稳定性测试

四、关键技术风险与应对
风险	概率	应对方案
Rust Xtensa 工具链编译问题	中	提前验证 Phase 1，备选方案：Rust 编译为静态库 .a 再链接
LVGL 3D 翻转动画性能不足	中	降级为 2D 淡入淡出动画，或预渲染动画帧存入 SPIFFS
JPEG 解码内存溢出	中	强制客户端预缩放，服务端限制图片尺寸 ≤ 360×360
SPIFFS 空间不足	低	分区表调整，为 SPIFFS 分配 ≥ 2MB（22张卡 × 约 75KB/张）
HTTP multipart 解析复杂	低	使用 esp-idf-svc 内置的 multipart 支持
五、分区表规划
# partitions.csv
nvs,       data, nvs,   0x9000,  0x5000,
phy_init,  data, phy,   0xF000,  0x1000,
factory,   app,  factory, 0x10000, 0x200000,  # 2MB 固件
spiffs,    data, spiffs, 0x210000, 0x1F0000,  # ~2MB 图片存储

Copy
六、开发顺序总结
Phase 1 (3天)  → Phase 2 (4天) → Phase 3 (3天)
                                      ↓
Phase 5 (5天) ← Phase 4 (2天) ←────────
      ↓
Phase 6 (2天) → Phase 7 (3天)

Copy
总计约 22 个工作日，可以并行推进 Phase 5 的 UI 骨架和 Phase 2-4 的 Rust 后端。