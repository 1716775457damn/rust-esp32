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
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 800); // 默认 80% 亮度
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
    
    // --- 强制复位尝试 (针对可能的 GPIO 3 复位脚) ---
    gpio_config_t rst_gpio_config = {
        .pin_bit_mask = (1ULL << 3),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_gpio_config);
    gpio_set_level((gpio_num_t)3, 0);
    vTaskDelay(pdMS_TO_TICKS(100)); // 拉低复位 100ms
    gpio_set_level((gpio_num_t)3, 1);
    vTaskDelay(pdMS_TO_TICKS(100)); // 释放复位并等待 100ms

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

    // 2. 初始化显示驱动
    bsp_display_init();

    // 3. 启动 Rust 核心
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
