#ifndef RUST_BRIDGE_H
#define RUST_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rust 导出的获取版本号的函数 (用于 Phase 1 测试 FFI)
const char* rust_get_version(void);

// AP & HTTP 服务器启动
void rust_start_ap_server(void);

#ifdef __cplusplus
}
#endif

#endif // RUST_BRIDGE_H
