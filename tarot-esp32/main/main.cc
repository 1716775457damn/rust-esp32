#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// 引入 Rust 接口
#include "rust_bridge.h"

extern "C" {
    // Stub for realpath which might be missing in some ESP-IDF configurations
    // Rust std expects this symbol to be present.
    char *realpath(const char *path, char *resolved_path) {
        if (resolved_path) {
            snprintf(resolved_path, 4096, "%s", path);
            return resolved_path;
        }
        return NULL;
    }

    void app_main(void);
}

void app_main(void)
{
    printf("========================================\n");
    printf("塔罗抽卡机 Phase 2: 网络初始化启动\n");
    printf("========================================\n");

    // 1. 初始化 NVS (Wi-Fi 必须)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 测试 C++ 调用 Rust 组件
    const char* version = rust_get_version();
    printf("✅ Rust Core 版本: %s\n", version);

    // 3. 调用 Rust 启动网络
    printf("-> [C++ 调用] 启动 Wi-Fi AP 和 HTTP Server...\n");
    rust_start_ap_server(); 

    while(true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
