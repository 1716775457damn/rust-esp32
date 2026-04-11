idf_component_register(
    SRCS "main.cc"
    INCLUDE_DIRS "ffi"
    REQUIRES tarot_core esp_lcd lvgl esp_lcd_st77916
    PRIV_REQUIRES nvs_flash esp_http_server esp_wifi esp_partition
)
