idf_component_register(
    SRCS "main.cc"
    INCLUDE_DIRS "ffi"
    REQUIRES tarot_core
    PRIV_REQUIRES nvs_flash esp_http_server esp_wifi
)
