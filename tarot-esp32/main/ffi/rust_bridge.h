#ifndef RUST_BRIDGE_H
#define RUST_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get Rust Core version string
const char* rust_get_version(void);

// Start Rust AP Server
void rust_start_ap_server(void);

// Trigger sound playback on C++ side from Rust
void rust_play_sound(const char* type);

#ifdef __cplusplus
    // C++ functions called by Rust
    void cpp_notify_card_ready(int slot_id);
}
#endif

#endif // RUST_BRIDGE_H
