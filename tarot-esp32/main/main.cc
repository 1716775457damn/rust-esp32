#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "esp_lcd_gc9a01.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "driver/i2s_std.h"


// 引入驱动配套文件
#include "ffi/rust_bridge.h"
#include "st77916_init_cmds.h"

// 使用系统默认字体避免链接错误
#define CARD_FONT &lv_font_montserrat_14

static const char *TAG = "tarot_main";

// --- 硬件引脚定义 (基于 C3 标准 SPI 模式) ---
#define LCD_HOST          SPI2_HOST
#define PIN_LCD_PCLK      1
#define PIN_LCD_MOSI      2
#define PIN_LCD_DC        0
#define PIN_LCD_CS        21
#define PIN_LCD_BL        20


#define PIN_I2C_SDA       3
#define PIN_I2C_SCL       4

// --- I2S 音频引脚 (对齐 PANBOPO) ---
#define PIN_I2S_BCLK      8
#define PIN_I2S_LRCK      6
#define PIN_I2S_DATA      5

// --- LVGL 配置 ---
#define LCD_H_RES         360
#define LCD_V_RES         360
#define LVGL_TICK_MS      5
#define LV_BUF_LINES      20 // 显存缓冲区行数 (C3 RAM 有限)

static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;
static esp_lcd_panel_handle_t panel_handle = NULL;

// --- Phase 5: 图片渲染专用缓冲区 ---
#define TAROT_IMG_SIZE (LCD_H_RES * LCD_V_RES * 2)
static lv_img_dsc_t tarot_img_dsc = {
    .header = {.cf = LV_IMG_CF_TRUE_COLOR, .always_zero = 0, .reserved = 0, .w = LCD_H_RES, .h = LCD_V_RES},
    .data_size = TAROT_IMG_SIZE,
    .data = NULL,
};
static lv_obj_t *ui_img_obj = NULL;

// --- 驱动回调 ---
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    // ESP_LOGD(TAG, "LVGL Flush: %d,%d to %d,%d", area->x1, area->y1, area->x2, area->y2);
    int offsetx1 = area->x1;
    int offsety1 = area->y1;
    int offsetx2 = area->x2;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
}

static void lvgl_tick_cb(void *arg) {
    lv_tick_inc(LVGL_TICK_MS);
}

// --- 初始化函数 ---

void bsp_display_backlight_init() {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK,
        .deconfigure      = false
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num       = PIN_LCD_BL,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER_0,
        .duty           = 0,
        .hpoint         = 0,
        .flags          = { .output_invert = 0 }
    };
    ledc_channel_config(&ledc_channel);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0); // 初始亮度 0%
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void bsp_display_init() {
    ESP_LOGI(TAG, "初始化 SPI 总线 (Standard mode)...");
    
    spi_bus_config_t buscfg = { };
    buscfg.sclk_io_num = PIN_LCD_PCLK;
    buscfg.mosi_io_num = PIN_LCD_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LCD_H_RES * LV_BUF_LINES * sizeof(uint16_t);
    
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    
    // --- 屏幕复位：采用软件复位，不占用 GPIO 3 (SDA) ---

    ESP_LOGI(TAG, "安装面板 IO (8-bit CMD, DC=0, Mode 0)...");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    
    esp_lcd_panel_io_spi_config_t io_config = { };
    io_config.cs_gpio_num = PIN_LCD_CS;
    io_config.dc_gpio_num = 0;
    io_config.spi_mode = 0; 
    io_config.pclk_hz = 15 * 1000 * 1000; // 降低至 15MHz 以平衡画质与功耗 (减少 BOD 触发)
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = notify_lvgl_flush_ready;
    io_config.user_ctx = &disp_drv;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = false;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "安装 ST77916 驱动...");
    st77916_vendor_config_t vendor_config = {
        .init_cmds = st77916_vendor_init_cmds,
        .init_cmds_size = sizeof(st77916_vendor_init_cmds) / sizeof(st77916_lcd_init_cmd_t),
    };

    
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1; 
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config; // 恢复：必须有 vendor_config 才能初始化驱动芯片
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
    
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true)); // 对齐小智项目：开启颜色反转
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
    ESP_LOGI(TAG, "ST77916 驱动初始化完成 (Reset 引脚: %d)", panel_config.reset_gpio_num);
    
    ESP_LOGI(TAG, "初始化 LVGL...");
    lv_init();
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(LCD_H_RES * LV_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, LCD_H_RES * LV_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = false
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LVGL_TICK_MS * 1000));

    bsp_display_backlight_init();

    // --- UI 样式：圆屏裁剪 ---
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0); // 黑色背景
    lv_obj_set_style_radius(lv_scr_act(), LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(lv_scr_act(), true, 0);

    // 初始化图片显示对象
    ui_img_obj = lv_img_create(lv_scr_act());
    lv_obj_center(ui_img_obj);
    lv_obj_set_style_opa(ui_img_obj, 0, 0); // 默认透明，等待动画
    
    // 默认背景文字
    lv_obj_t * welcome_label = lv_label_create(lv_scr_act());
    lv_label_set_text(welcome_label, "Ready for Tarot");
    lv_obj_set_style_text_font(welcome_label, CARD_FONT, 0);
    lv_obj_align(welcome_label, LV_ALIGN_BOTTOM_MID, 0, -40);
}

// --- Phase 6: I2S & ES8311 音频播放系统 ---
static i2s_chan_handle_t tx_handle = NULL;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t es8311_dev_handle = NULL;

#define ES8311_ADDR 0x18

// 简化的 ES8311 唤醒序列 (写入寄存器 + 错误检测)
esp_err_t es8311_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    esp_err_t ret = i2c_master_transmit(es8311_dev_handle, buf, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C 写入失败 [0x%02X]=0x%02X: %s", reg, val, esp_err_to_name(ret));
    }
    return ret;
}

void bsp_audio_init() {
    ESP_LOGI(TAG, "正在强制拉低 GPIO 11 以开启功放...");
    gpio_config_t pa_cfg = {
        .pin_bit_mask = (1ULL << 11),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pa_cfg);
    gpio_set_level(GPIO_NUM_11, 0); 
    vTaskDelay(pdMS_TO_TICKS(10));

    // 1. 初始化 I2C 总线
    ESP_LOGI(TAG, "初始化 I2C (SDA:%d SCL:%d)...", PIN_I2C_SDA, PIN_I2C_SCL);
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)PIN_I2C_SDA,
        .scl_io_num = (gpio_num_t)PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = true }
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &es8311_dev_handle));

    // 2. 唤醒 ES8311 官方驱动极速锁定序列 (针对无 MCLK 优化的 16kHz 环境)
    ESP_LOGI(TAG, "正在唤醒 ES8311 (官方驱动位对齐模式)...");
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // A. 基础抗干扰与预复位
    es8311_write_reg(0x44, 0x08); // 官方双写抗干扰逻辑
    es8311_write_reg(0x44, 0x08); 
    es8311_write_reg(0x45, 0x00); // GPIO 设为默认模式
    es8311_write_reg(0x01, 0x30); // 官方推荐: 预设电源管理开启
    es8311_write_reg(0x02, 0x10); // 预备 PLL 锁相环
    es8311_write_reg(0x03, 0x10); // 官方 ADC 开屏
    es8311_write_reg(0x16, 0x24); // 官方推荐 ALC 保持值
    es8311_write_reg(0x04, 0x10); // DAC OSR 设置
    es8311_write_reg(0x00, 0x80); // 设为 Slave 模式 (由 ESP32 提供 BCLK/LRCLK)
    
    // B. *** 核心锁定：无 MCLK 下的 PLL 倍频对齐 ***
    // 对于 16kHz, 官方驱动计算得出 REG01=0xBF, REG02=0x18 (即 datmp=3, 倍频=x8)
    es8311_write_reg(0x01, 0xBF); // 开启全域时钟电源
    es8311_write_reg(0x02, 0x18); // 关键：开启 x8 倍频，同步 BCLK 信号
    es8311_write_reg(0x06, 0x03); // BCLK 分频对齐 (BCLK_DIV=4)
    
    // C. 模拟通路与协议深度对齐
    es8311_write_reg(0x09, 0x0C); // Philips 16bit 格式
    es8311_write_reg(0x0A, 0x0C); 
    es8311_write_reg(0x0E, 0x02); // 官方系统偏置开启
    es8311_write_reg(0x12, 0x00); // DAC 偏置稳定
    es8311_write_reg(0x13, 0x10); // Bias 驱动开启
    es8311_write_reg(0x14, 0x1A); // 模拟增益初始化 (+0dB)
    es8311_write_reg(0x1B, 0x0A); // VREF 稳定电压
    es8311_write_reg(0x1C, 0x6A); // VREF 驱动强度
    
    // D. 动力输出与最终音量精调
    es8311_write_reg(0x0D, 0x01); // 全硬件 PowerUp
    es8311_write_reg(0x32, 0x90); // *** 音量优化: 从 0xBF(80%) 降至 0x90(~55%) ***
    es8311_write_reg(0x31, 0x00); // 开启 DAC 信号通路 (Mute=0)
    vTaskDelay(pdMS_TO_TICKS(50)); 
    
    ESP_LOGI(TAG, "ES8311 唤醒完毕 (官方驱动位对齐模式)");

    // 3. 初始化 I2S 频道 (对齐官方时序: 24kHz, Philips Standard)
    i2s_chan_config_t i2s_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_cfg.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&i2s_chan_cfg, &tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000), 
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)PIN_I2S_BCLK,
            .ws = (gpio_num_t)PIN_I2S_LRCK,
            .dout = (gpio_num_t)PIN_I2S_DATA,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    std_cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_16BIT;
    std_cfg.slot_cfg.bit_shift = true;
    std_cfg.slot_cfg.left_align = true; // *** 核心修正: 根据参考工程开启左对齐 ***
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}

static void play_wav(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "无法打开音效文件: %s", filepath);
        return;
    }

    ESP_LOGI(TAG, "开始播放音效 (16kHz 立体声克隆模式): %s", filepath);
    gpio_set_level(GPIO_NUM_7, 0); // 调暗背光
    
    fseek(f, 44, SEEK_SET);
    int16_t mono_buf[256];
    int16_t stereo_buf[512]; // 每一个采样点复制成一对
    size_t samples_read, bytes_written;
    int total_bytes = 0;

    while ((samples_read = fread(mono_buf, sizeof(int16_t), 256, f)) > 0) {
        for (int i = 0; i < samples_read; i++) {
            stereo_buf[i * 2] = mono_buf[i];     // 左声道
            stereo_buf[i * 2 + 1] = mono_buf[i]; // 右声道
        }
        i2s_channel_write(tx_handle, stereo_buf, samples_read * 4, &bytes_written, portMAX_DELAY);
        total_bytes += bytes_written;
    }
    
    gpio_set_level(GPIO_NUM_7, 1); // 恢复背光
    ESP_LOGI(TAG, "音频播放完毕，已写入 I2S 总量: %d 字节", total_bytes);
    fclose(f);
}

// 异步播放任务封装
extern "C" void rust_play_sound(const char* type) {
    if (type == NULL) return;
    ESP_LOGI(TAG, "FFI: 收到音频播放请求 [%s]", type);
    
    char path[64];
    if (strcmp(type, "shuffle") == 0) {
        snprintf(path, sizeof(path), "/spiffs/shuffle.wav");
    } else {
        snprintf(path, sizeof(path), "/spiffs/draw.wav");
    }
    
    play_wav(path);
}

// --- 动画回调 ---
static void anim_opa_cb(void * var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, v, 0);
}

// --- Phase 5: 从 SPIFFS 加载并显示图片 ---
// --- Phase 5: 从 SPIFFS 加载并显示图片 ---
static void DisplayCardFromSpiffs(int slot_id) {
    char filename[64];
    // 根据 Rust 端的解码逻辑，文件名应为 /spiffs/tarot_0.rgb565
    sprintf(filename, "/spiffs/tarot_%d.rgb565", slot_id);
    
    FILE* f = fopen(filename, "rb");
    if (!f) {
        ESP_LOGE(TAG, "无法打开文件: %s", filename);
        return;
    }

    // 获取文件大小进行校验 (预期 360*360*2 = 259200 字节)
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "开始渲染图片: %s (大小: %ld)", filename, size);

    // 采用分块渲染模式，每次处理 10 行像素以更精细地控制电流
    const int lines_per_chunk = 10;
    const int chunk_size = LCD_H_RES * lines_per_chunk * 2;
    uint8_t* chunk_buf = (uint8_t*)heap_caps_malloc(chunk_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    if (!chunk_buf) {
        ESP_LOGE(TAG, "初始化分块缓冲区失败！");
        fclose(f);
        return;
    }

    // 循环读取并发送到屏幕
    for (int y = 0; y < LCD_V_RES; y += lines_per_chunk) {
        int actual_lines = (y + lines_per_chunk <= LCD_V_RES) ? lines_per_chunk : (LCD_V_RES - y);
        int read_bytes = actual_lines * LCD_H_RES * 2;
        
        if (fread(chunk_buf, 1, read_bytes, f) != read_bytes) {
            ESP_LOGE(TAG, "文件读取异常 at y=%d", y);
            break;
        }
        
        esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_H_RES, y + actual_lines, chunk_buf);
        
        // 关键：加入节奏控制，防止瞬时电流过大触发 BOD，同时让图片像帘幕一样平滑展开
        vTaskDelay(pdMS_TO_TICKS(12));
    }
    
    // 分阶段开启背光以降低瞬间峰值电流
    gpio_config_t bl_gpio_config = {
        .pin_bit_mask = (1ULL << 20),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&bl_gpio_config);
    gpio_set_level((gpio_num_t)20, 0); // 先关闭
    gpio_set_level((gpio_num_t)20, 1);

    free(chunk_buf);
    fclose(f);

    // 更新 LVGL 图像对象
    lv_img_set_src(ui_img_obj, &tarot_img_dsc);
    
    // 启动淡入动画
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui_img_obj);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 800);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    // 不需要调用 lv_obj_invalidate，因为我们已经手动精准刷屏
    ESP_LOGI(TAG, "图片显示更新成功 (硬件直接渲染)");
}

extern "C" {
    void app_main(void);
}

void app_main(void)
{
    printf("========================================\n");
    printf("塔罗抽卡机 Phase 4: 屏幕 & LVGL 启动\n");
    printf("========================================\n");

    // 1. 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化核心外设 (软启动模式)
    bsp_display_init(); // 此时背光为 0%
    bsp_audio_init();   // 加载芯片配置

    // 3. 延时进入，避开上电电流冲击
    vTaskDelay(pdMS_TO_TICKS(200));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 800); // 渐进开启背光
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

    // 4. 启动 Rust 核心
    const char* version = rust_get_version();
    printf("✅ Rust Core 版本: %s\n", version);
    rust_start_ap_server(); 

    // UI 主循环
    while(true) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Rust 通过 FFI 调用此函数
// Rust 通过 FFI 调用此函数
extern "C" void cpp_notify_card_ready(int slot_id) {
    ESP_LOGI(TAG, "收到 Rust 通知: 卡片已就绪 (Slot %d)", slot_id);
    
    // 3. 进入 Phase 5: 开启卡片显示流程
    DisplayCardFromSpiffs(slot_id); 
}
