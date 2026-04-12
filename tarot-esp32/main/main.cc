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

// 引入驱动配套文件与 Rust FFI
#include "ffi/rust_bridge.h"
#include "st77916_init_cmds.h"

static const char *TAG = "tarot_main";

/**
 * @brief Configuration Namespace
 * Unified management of pins, display parameters, and bus frequencies.
 */
namespace Config {
    static constexpr int LCD_H_RES = 360;
    static constexpr int LCD_V_RES = 360;
    static constexpr int CARD_DIM = 220;
    static constexpr int BORDER_GAP = 70; // (360 - 220) / 2
    static constexpr int LV_BUF_LINES = 20;
    static constexpr uint32_t SPI_FREQ = 40 * 1000 * 1000; // V4.3: Boosted for smaller sync window
    
    // Bus Configuration
    static constexpr spi_host_device_t LCD_HOST = SPI2_HOST;
    
    // Pin Configuration
    static constexpr gpio_num_t PIN_LCD_PCLK = GPIO_NUM_1;
    static constexpr gpio_num_t PIN_LCD_MOSI = GPIO_NUM_2;
    static constexpr gpio_num_t PIN_LCD_DC   = GPIO_NUM_0;
    static constexpr gpio_num_t PIN_LCD_CS   = GPIO_NUM_21; // Reference pin
    static constexpr gpio_num_t PIN_LCD_BL   = GPIO_NUM_20;
    
    static constexpr gpio_num_t PIN_I2C_SDA  = GPIO_NUM_3;
    static constexpr gpio_num_t PIN_I2C_SCL  = GPIO_NUM_4;
    
    static constexpr gpio_num_t PIN_I2S_BCLK = GPIO_NUM_8;
    static constexpr gpio_num_t PIN_I2S_LRCK = GPIO_NUM_6;
    static constexpr gpio_num_t PIN_I2S_DATA = GPIO_NUM_5;
}

/**
 * @brief Async Task Event Bus
 */
enum class TarotEvent { SHUFFLE, DISPLAY_INFO, REVEAL, PLAY_SOUND };
struct TarotMessage {
    TarotEvent type;
    int slot_id;
    char text_1[32]; 
    char text_2[64]; 
};

/**
 * @brief UI Message Bus (Thread Safety)
 */
enum class UIEvent { SET_TEXT_NAME, SET_TEXT_KEYS, SHOW_SPINNER, HIDE_SPINNER, SHOW_CARD, FADE_IN };
struct UIMessage {
    UIEvent event;
    int val;
    char text[64];
};

// --- Reliability: Static memory pool to avoid heap fragmentation ---
static uint8_t s_render_chunk_buf[Config::CARD_DIM * 10 * 2]; 

static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;
static esp_lcd_panel_handle_t panel_handle = NULL;
static SemaphoreHandle_t lcd_mutex = NULL;
static SemaphoreHandle_t dma_done_sem = NULL;         // V4.3: DMA Sync Signal
static volatile bool g_card_rendering = false;       // V4.3: Task Isolation Flag
static QueueHandle_t ritual_queue = NULL;
static QueueHandle_t ui_queue = NULL; 
static QueueHandle_t audio_queue = NULL; 

/**
 * @brief RAII LCD Resource Lock
 */
class LCDLock {
public:
    LCDLock() { if (lcd_mutex) xSemaphoreTake(lcd_mutex, portMAX_DELAY); }
    ~LCDLock() { if (lcd_mutex) xSemaphoreGive(lcd_mutex); }
};

// --- Phase 7: UI 2.0 Premium Ritual UI variables ---
static lv_obj_t * ui_card_img = NULL;
static lv_obj_t * ui_name_label = NULL;
static lv_obj_t * ui_keys_label = NULL;
static lv_obj_t * ui_spinner = NULL;
static lv_obj_t * ui_ritual_bg = NULL;

// --- Driver Callbacks ---
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    
    // V4.3: Notify the ritual task that DMA transfer is complete
    if (dma_done_sem) {
        BaseType_t high_task_wakeup = pdFALSE;
        xSemaphoreGiveFromISR(dma_done_sem, &high_task_wakeup);
        return high_task_wakeup == pdTRUE;
    }
    return false;
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    LCDLock lock; // 自动获取与释放锁
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
}

static void lvgl_tick_cb(void *arg) {
    lv_tick_inc(5); // Fixed 5ms step
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
        .gpio_num       = Config::PIN_LCD_BL,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER_0,
        .duty           = 0,
        .hpoint         = 0,
        .flags          = { .output_invert = 0 }
    };
    ledc_channel_config(&ledc_channel);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0); // Start at 0%
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void bsp_display_init() {
    ESP_LOGI(TAG, "初始化 SPI 总线 (Standard mode)...");
    
    spi_bus_config_t buscfg = { };
    buscfg.sclk_io_num = Config::PIN_LCD_PCLK;
    buscfg.mosi_io_num = Config::PIN_LCD_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = Config::LCD_H_RES * Config::LV_BUF_LINES * sizeof(uint16_t);
    
    ESP_ERROR_CHECK(spi_bus_initialize(Config::LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));
    
    // --- Screen Reset: Software reset to avoid using GPIO 3 (SDA) ---

    ESP_LOGI(TAG, "Install Panel IO (8-bit CMD, DC=0, Mode 0)...");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    
    esp_lcd_panel_io_spi_config_t io_config = { };
    io_config.cs_gpio_num = Config::PIN_LCD_CS;
    io_config.dc_gpio_num = Config::PIN_LCD_DC;
    io_config.spi_mode = 0; 
    io_config.pclk_hz = Config::SPI_FREQ; 
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = notify_lvgl_flush_ready;
    io_config.user_ctx = &disp_drv;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)Config::LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install ST77916 Driver...");
    st77916_vendor_config_t vendor_config = {
        .init_cmds = st77916_vendor_init_cmds,
        .init_cmds_size = sizeof(st77916_vendor_init_cmds) / sizeof(st77916_lcd_init_cmd_t),
        .flags = { .use_qspi_interface = 0 } 
    };

    
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = -1; 
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config; // 恢复：必须有 vendor_config 才能初始化驱动芯片
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
    
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true)); // Alignment: Enable color inversion
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    
    ESP_LOGI(TAG, "ST77916 Driver Init Complete (Reset Pin: %d)", panel_config.reset_gpio_num);
    
    ESP_LOGI(TAG, "Init LVGL...");
    lv_init();
    lv_color_t *buf1 = (lv_color_t *)heap_caps_malloc(Config::LCD_H_RES * Config::LV_BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, Config::LCD_H_RES * Config::LV_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = Config::LCD_H_RES;
    disp_drv.ver_res = Config::LCD_V_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // Init core locks and async queues
    lcd_mutex = xSemaphoreCreateMutex();
    dma_done_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(dma_done_sem); // Initial state: Ready for first transfer
    
    ritual_queue = xQueueCreate(10, sizeof(TarotMessage));
    ui_queue = xQueueCreate(20, sizeof(UIMessage));
    audio_queue = xQueueCreate(5, sizeof(char) * 64);

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 5000));

    bsp_display_backlight_init();

    // --- UI 样式：圆屏裁剪 ---
    // 1. Full-screen Immersive Background (360x360)
    ui_ritual_bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_ritual_bg, Config::LCD_H_RES, Config::LCD_V_RES);
    lv_obj_center(ui_ritual_bg);
    lv_obj_set_style_bg_color(ui_ritual_bg, lv_color_make(13, 4, 18), 0); // Mystic Purple
    lv_obj_set_style_border_width(ui_ritual_bg, 0, 0);
    lv_obj_set_style_radius(ui_ritual_bg, LV_RADIUS_CIRCLE, 0);

    // 2. Card Image Viewport (Downsized to 220 to fit Flash)
    ui_card_img = lv_img_create(ui_ritual_bg);
    lv_obj_set_size(ui_card_img, Config::CARD_DIM, Config::CARD_DIM); 
    lv_obj_align(ui_card_img, LV_ALIGN_TOP_MID, 0, 30); // Lowered Y offset
    lv_obj_add_flag(ui_card_img, LV_OBJ_FLAG_HIDDEN); 

    // 3. Card Name Label (Golden theme)
    ui_name_label = lv_label_create(ui_ritual_bg);
    lv_obj_set_style_text_color(ui_name_label, lv_color_make(255, 215, 0), 0);
    lv_obj_align(ui_name_label, LV_ALIGN_TOP_MID, 0, 285);
    lv_obj_set_style_text_align(ui_name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ui_name_label, "Celestial Tarot");

    // 4. Interpretation Label
    ui_keys_label = lv_label_create(ui_ritual_bg);
    lv_obj_set_style_text_color(ui_keys_label, lv_color_make(180, 160, 255), 0);
    lv_obj_set_style_text_font(ui_keys_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ui_keys_label, LV_ALIGN_TOP_MID, 0, 315);
    lv_label_set_text(ui_keys_label, "Ready for your fate");

    // 5. Shuffle Ritual Spinner
    ui_spinner = lv_spinner_create(ui_ritual_bg, 1000, 60);
    lv_obj_set_size(ui_spinner, 240, 240);
    lv_obj_center(ui_spinner);
    lv_obj_set_style_arc_color(ui_spinner, lv_color_make(218, 165, 32), LV_PART_INDICATOR);
    lv_obj_add_flag(ui_spinner, LV_OBJ_FLAG_HIDDEN);
}

// --- Phase 6: I2S & ES8311 音频播放系统 ---
static i2s_chan_handle_t tx_handle = NULL;
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static i2c_master_dev_handle_t es8311_dev_handle = NULL;

#define ES8311_ADDR 0x18

// --- Phase 8: FFI 接口实现 (与 Rust 协同) ---
extern "C" void cpp_ui_start_shuffle() {
    TarotMessage m = { .type = TarotEvent::SHUFFLE, .slot_id = 0, .text_1 = {0}, .text_2 = {0} };
    xQueueSend(ritual_queue, &m, 0);
}

extern "C" void cpp_ui_display_info(const char* name, const char* keys) {
    TarotMessage m = { .type = TarotEvent::DISPLAY_INFO, .slot_id = 0, .text_1 = {0}, .text_2 = {0} };
    strncpy(m.text_1, name ? name : "", sizeof(m.text_1) - 1);
    strncpy(m.text_2, keys ? keys : "", sizeof(m.text_2) - 1);
    xQueueSend(ritual_queue, &m, 0);
}

/**
 * @brief Error Defense: Draw a placeholder rectangle if asset is missing.
 */
static void DrawErrorPlaceholder() {
    LCDLock lock;
    uint16_t* color_buf = (uint16_t*)s_render_chunk_buf;
    // Fill with mystic purple placeholder
    for(int i=0; i < Config::CARD_DIM * 10; i++) color_buf[i] = 0x4008; 
    
    for(int y=0; y < Config::CARD_DIM; y += 10) {
        esp_lcd_panel_draw_bitmap(panel_handle, Config::BORDER_GAP, 30 + y, 
                                 Config::BORDER_GAP + Config::CARD_DIM, 30 + y + 10, color_buf);
    }
}

// Direct Hardware Render: Chunked draw (Granular locking & DMA Sync)
static void DisplayCardToHardware(int card_idx) {
    char filename[64];
    sprintf(filename, "/spiffs/%d.bin", card_idx);
    
    FILE* f = fopen(filename, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Asset Missing: %s", filename);
        DrawErrorPlaceholder();
        return;
    }

    g_card_rendering = true; // Block LVGL refresh to protect pixels
    const int chunk_size = Config::CARD_DIM * 10 * 2;
    
    for (int y = 0; y < Config::CARD_DIM; y += 10) {
        int y_start = 30 + y;
        int y_end   = 30 + y + 10;
        if (y_start >= y_end || y_end > Config::LCD_V_RES) break;

        // V4.3: Wait for previous DMA stripe to finish before overwriting buffer
        if (xSemaphoreTake(dma_done_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "DMA Sync Timeout at y=%d", y);
        }

        // A. fread with defensive retry
        size_t read = 0;
        int retry = 3;
        while (retry-- > 0) {
            read = fread(s_render_chunk_buf, 1, chunk_size, f);
            if (read > 0) break;
            vTaskDelay(pdMS_TO_TICKS(5)); 
        }
        
        if (read == 0) {
            ESP_LOGE(TAG, "Flash Read Error at y=%d", y);
            xSemaphoreGive(dma_done_sem); // Release if failing
            break;
        }
        
        // B. Push to hardware with exclusive lock
        {
            LCDLock lock;
            esp_lcd_panel_draw_bitmap(panel_handle, Config::BORDER_GAP, y_start, 
                                    Config::BORDER_GAP + Config::CARD_DIM, y_end, s_render_chunk_buf);
        }
    }
    
    // Ensure last stripe is finished before releasing state
    xSemaphoreTake(dma_done_sem, pdMS_TO_TICKS(100));
    xSemaphoreGive(dma_done_sem);
    g_card_rendering = false; 
    
    fclose(f);
}

// --- Reveal Fade-In Animation ---
static void UI_FadeIn(lv_obj_t* obj) {
    if (!obj) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 800);
    lv_anim_set_exec_cb(&a, [](void* var, int32_t v){ lv_obj_set_style_opa((lv_obj_t*)var, v, 0); });
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void play_wav(const char* filepath) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open sound file: %s", filepath);
        return;
    }

    ESP_LOGI(TAG, "Start playback (16kHz Stereo): %s", filepath);
    gpio_set_level(GPIO_NUM_7, 0); 
    
    fseek(f, 44, SEEK_SET);
    
    // --- Memory Optimization: Use pre-allocated heap instead of stack ---
    static int16_t* mono_buf = nullptr;
    static int16_t* stereo_buf = nullptr;
    if (!mono_buf) mono_buf = (int16_t*)heap_caps_malloc(256 * sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!stereo_buf) stereo_buf = (int16_t*)heap_caps_malloc(512 * sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    size_t samples_read, bytes_written;
    int total_bytes = 0;

    while ((samples_read = fread(mono_buf, sizeof(int16_t), 256, f)) > 0) {
        for (int i = 0; i < (int)samples_read; i++) {
            stereo_buf[i * 2] = mono_buf[i];     
            stereo_buf[i * 2 + 1] = mono_buf[i]; 
        }
        i2s_channel_write(tx_handle, stereo_buf, samples_read * 4, &bytes_written, portMAX_DELAY);
        total_bytes += bytes_written;
    }
    
    gpio_set_level(GPIO_NUM_7, 1); 
    ESP_LOGI(TAG, "Playback Complete: %d bytes", total_bytes);
    fclose(f);
}

/**
 * @brief Audio Task (Priority 3)
 */
static void audio_task(void *arg) {
    char path[64];
    ESP_LOGI(TAG, "Audio task started...");
    while (xQueueReceive(audio_queue, path, portMAX_DELAY)) {
        play_wav(path);
    }
}

/**
 * @brief Async Ritual Task: Handles render & sound without blocking HTTP
 */
static void tarot_ritual_task(void *arg) {
    TarotMessage msg;
    ESP_LOGI(TAG, "Ritual task started...");
    
    while (xQueueReceive(ritual_queue, &msg, portMAX_DELAY)) {
        switch (msg.type) {
            case TarotEvent::SHUFFLE:
                {
                    UIMessage m = { UIEvent::SHOW_SPINNER, 0, "" }; 
                    xQueueSend(ui_queue, &m, 0);
                    m = { UIEvent::SET_TEXT_NAME, 0, "Consulting Stars..." };
                    xQueueSend(ui_queue, &m, 0);
                }
                break;
                
            case TarotEvent::DISPLAY_INFO:
                {
                    UIMessage m = { UIEvent::SET_TEXT_NAME, 0, "" };
                    strncpy(m.text, msg.text_1, sizeof(m.text)-1);
                    xQueueSend(ui_queue, &m, 0);
                    m = { UIEvent::SET_TEXT_KEYS, 0, "" };
                    strncpy(m.text, msg.text_2, sizeof(m.text)-1);
                    xQueueSend(ui_queue, &m, 0);
                }
                break;
                
            case TarotEvent::REVEAL: {
                {
                    UIMessage m = { UIEvent::HIDE_SPINNER, 0, "" };
                    xQueueSend(ui_queue, &m, 0);
                }
                
                // Allow GUI a window to refresh
                vTaskDelay(pdMS_TO_TICKS(20)); 
                
                DisplayCardToHardware(msg.slot_id); // Now slot_id is the actual card index
                
                {
                    UIMessage m = { UIEvent::FADE_IN, 0, "" };
                    xQueueSend(ui_queue, &m, 0);
                }
                break;
            }
            case TarotEvent::PLAY_SOUND:
                xQueueSend(audio_queue, msg.text_2, 0);
                break;
        }
    }
}

extern "C" void cpp_notify_card_ready(int slot_id) {
    TarotMessage m = { .type = TarotEvent::REVEAL, .slot_id = slot_id, .text_1 = {0}, .text_2 = {0} };
    xQueueSend(ritual_queue, &m, 0);
}

// ES8311 Wakeup Sequence
esp_err_t es8311_write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    esp_err_t ret = i2c_master_transmit(es8311_dev_handle, buf, 2, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C Write Failed [0x%02X]=0x%02X: %s", reg, val, esp_err_to_name(ret));
    }
    return ret;
}

void bsp_audio_init() {
    ESP_LOGI(TAG, "Forcing GPIO 11 LOW to enable PA...");
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

    // 1. Init I2C Bus
    ESP_LOGI(TAG, "Init I2C (SDA:%d SCL:%d)...", Config::PIN_I2C_SDA, Config::PIN_I2C_SCL);
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = Config::PIN_I2C_SDA,
        .scl_io_num = Config::PIN_I2C_SCL,
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
        .scl_wait_us = 0,
        .flags = { .disable_ack_check = 0 }
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &es8311_dev_handle));

    // 2. Wakeup ES8311 (Official Bit-aligned mode for 16kHz)
    ESP_LOGI(TAG, "Waking up ES8311...");
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // A. Anti-interference & Pre-reset
    es8311_write_reg(0x44, 0x08); 
    es8311_write_reg(0x44, 0x08); 
    es8311_write_reg(0x45, 0x00); 
    es8311_write_reg(0x01, 0x30); 
    es8311_write_reg(0x02, 0x10); 
    es8311_write_reg(0x03, 0x10); 
    es8311_write_reg(0x16, 0x24); 
    es8311_write_reg(0x04, 0x10); 
    es8311_write_reg(0x00, 0x80); 
    
    // B. *** PLL Lock for non-MCLK environment ***
    // Official: REG01=0xBF, REG02=0x18 for 16kHz
    es8311_write_reg(0x01, 0xBF); 
    es8311_write_reg(0x02, 0x18); 
    es8311_write_reg(0x06, 0x03); 
    
    // C. Analog path & Protocol alignment
    es8311_write_reg(0x09, 0x0C); 
    es8311_write_reg(0x0A, 0x0C); 
    es8311_write_reg(0x0E, 0x02); 
    es8311_write_reg(0x12, 0x00); 
    es8311_write_reg(0x13, 0x10); 
    es8311_write_reg(0x14, 0x1A); 
    es8311_write_reg(0x1B, 0x0A); 
    es8311_write_reg(0x1C, 0x6A); 
    
    // D. Power & Volume Refinement
    es8311_write_reg(0x0D, 0x01); 
    es8311_write_reg(0x32, 0x90); // Vol: ~55%
    es8311_write_reg(0x31, 0x00); // DAC Open (Mute=0)
    vTaskDelay(pdMS_TO_TICKS(50)); 
    
    ESP_LOGI(TAG, "ES8311 Wakeup Complete");

    // 3. Init I2S Channel (24kHz, Philips Standard)
    i2s_chan_config_t i2s_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_chan_cfg.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&i2s_chan_cfg, &tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = 16000,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = Config::PIN_I2S_BCLK,
            .ws = Config::PIN_I2S_LRCK,
            .dout = Config::PIN_I2S_DATA,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    std_cfg.slot_cfg.ws_width = I2S_DATA_BIT_WIDTH_16BIT;
    std_cfg.slot_cfg.bit_shift = true;
    std_cfg.slot_cfg.left_align = true; // Alignment: Left-aligned per reference
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}


// Async playback wrapper
extern "C" void rust_play_sound(const char* type) {
    if (type == NULL) return;
    TarotMessage m = { .type = TarotEvent::PLAY_SOUND, .slot_id = 0, .text_1 = {0}, .text_2 = {0} };
    
    // Reuse text_2 for path
    if (strcmp(type, "shuffle") == 0) {
        strcpy(m.text_2, "/spiffs/shuffle.wav");
    } else {
        strcpy(m.text_2, "/spiffs/draw.wav");
    }
    xQueueSend(ritual_queue, &m, 0);
}


/**
 * @brief GUI Task: Exclusive LVGL operations
 */
static void gui_task(void *arg) {
    UIMessage msg;
    ESP_LOGI(TAG, "GUI Task Started (Stack: 16KB)");
    while(true) {
        // 1. Consume UI Command Queue
        while (xQueueReceive(ui_queue, &msg, 0)) {
            switch (msg.event) {
                case UIEvent::SET_TEXT_NAME:
                    if (ui_name_label) lv_label_set_text(ui_name_label, msg.text);
                    break;
                case UIEvent::SET_TEXT_KEYS:
                    if (ui_keys_label) lv_label_set_text(ui_keys_label, msg.text);
                    break;
                case UIEvent::SHOW_SPINNER:
                    if (ui_spinner) lv_obj_clear_flag(ui_spinner, LV_OBJ_FLAG_HIDDEN);
                    break;
                case UIEvent::HIDE_SPINNER:
                    if (ui_spinner) lv_obj_add_flag(ui_spinner, LV_OBJ_FLAG_HIDDEN);
                    break;
                case UIEvent::FADE_IN:
                    UI_FadeIn(ui_name_label);
                    UI_FadeIn(ui_keys_label);
                    break;
                case UIEvent::SHOW_CARD:
                    if (ui_card_img) lv_obj_clear_flag(ui_card_img, LV_OBJ_FLAG_HIDDEN);
                    break;
            }
        }
        
        // 2. 驱动 LVGL 定时器 (仅在非硬件渲染期间刷新，防止背景层覆盖卡面)
        if (!g_card_rendering) {
            lv_timer_handler();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" void app_main(void)
{
    printf("========================================\n");
    printf("Tarot Machine UI 3.0: High Stability\n");
    printf("========================================\n");

    // 1. Init NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Init Core Peripherals
    bsp_display_init(); 
    bsp_audio_init();   

    // Start Ritual Task (Prio 4)
    xTaskCreate(tarot_ritual_task, "RitualTask", 16384, NULL, 4, NULL);
    
    // Start GUI Task (Prio 5)
    xTaskCreate(gui_task, "GUITask", 16384, NULL, 5, NULL);

    // Start Audio Task (Prio 3)
    xTaskCreate(audio_task, "AudioTask", 4096, NULL, 3, NULL);

    // 3. Start Rust Core
    const char* version = rust_get_version();
    printf("✅ Rust Core Version: %s\n", version);
    rust_start_ap_server(); 

    // 4. Fade in backlight
    vTaskDelay(pdMS_TO_TICKS(200));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 800);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    
    // System monitoring loop
    while(true) {
        ESP_LOGI("SYS", "Free Heap: %lu bytes, Max Block: %lu bytes", 
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
