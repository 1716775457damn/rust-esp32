set(RUST_LIB_NAME "libtarot_core_rust.a")

idf_component_register(
    SRCS "dummy.c"
    REQUIRES esp_http_server esp_wifi nvs_flash esp_netif esp_event
)


# 解决 Newlib/nosys 与 ESP-IDF vfs 之间的符号冲突 (如 opendir/closedir 等)
target_link_options(${COMPONENT_LIB} PUBLIC "-Wl,--allow-multiple-definition")

# 创建一个 IMPORTED target 代表 Rust 库 (必须是 GLOBAL，以便跨组件寻找或者确保作用域)
add_library(rust_core STATIC IMPORTED GLOBAL)
set_property(TARGET rust_core PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/${RUST_LIB_NAME})
set_property(TARGET rust_core PROPERTY INTERFACE_LINK_LIBRARIES 
    idf::esp_http_server 
    idf::esp_wifi 
    idf::nvs_flash 
    idf::esp_netif 
    idf::esp_event 
    idf::vfs 
    idf::newlib
)

# 将 Rust 库链接到当前组件
target_link_libraries(${COMPONENT_LIB} PUBLIC rust_core)

# 使用 CMAKE_COMMAND -E copy 代替 cp 保证在 Windows 环境下不报错
# 注入 CARGO_TARGET_DIR=C:/t 以绕过 Windows 路径长度限制
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${RUST_LIB_NAME}
    COMMAND ${CMAKE_COMMAND} -E env MCU=esp32c3 CARGO_TARGET_DIR=C:/t cargo +esp build --release
    COMMAND ${CMAKE_COMMAND} -E copy C:/t/riscv32imc-esp-espidf/release/libtarot_core.a ${CMAKE_CURRENT_BINARY_DIR}/${RUST_LIB_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/lib.rs
)

add_custom_target(tarot_core_target ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${RUST_LIB_NAME})
add_dependencies(${COMPONENT_LIB} tarot_core_target)
