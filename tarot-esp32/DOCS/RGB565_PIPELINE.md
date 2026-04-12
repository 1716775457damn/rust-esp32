# High-Speed RGB565 Asset Pipeline: Technical Deep Dive

This document records the architectural logic behind the Tarot ESP32 UI 3.0 performance optimization.

## 1. The Core Philosophy: "Calculate Once, Run Forever"
Embedded systems (ESP32-C3) are often CPU-constrained but have relatively fast serial storage (Flash). Traditional JPEG decoding consumes significant CPU cycles and RAM. By moving the decoding phase to the PC during development, we achieved:
- **Zero Runtime Decoding Overhead**.
- **Minimal RAM Footprint** (No decompression buffers needed).

## 2. Asset Pipeline Logic (`prepare_assets.py`)
The pipeline follows these steps for each of the 78 cards:
1. **Normalization**: Rescale standard Tarot images to exactly **220x220** pixels.
2. **Color Conversion**: Convert 24-bit RGB (888) to **16-bit RGB565** (Upper 5-bit Red, Middle 6-bit Green, Lower 5-bit Blue).
3. **Big-Endian Packing**: Pack the 16-bit values into a binary stream.
    - Each pixels takes 2 bytes. Total file size: `220 * 220 * 2 = 96,800 bytes`.
4. **Metadata Sync**: Generate `names.txt` to map numeric indices to logic.

## 3. Hardware Interleaving (`main.cc`)
The C++ side implements a "Stream-to-Bus" rendering pattern. Instead of loading the whole file into RAM, it reads the card in small chunks (strips of 10 rows):
```cpp
for (int y = 0; y < CARD_DIM; y += 10) {
    // Read 10 rows of raw pixels into a static reusable buffer
    fread(color_buf, 1, CARD_DIM * 10 * 2, f);
    // Push the pixels directly to the LCD controller via SPI DMA
    esp_lcd_panel_draw_bitmap(..., color_buf);
}
```
### Why 10 rows?
- **Buffer Efficiency**: Small enough to fit in IRAM/SRAM without causing heap fragmentation.
- **Latency Masking**: While the SPI bus is busy sending one strip, the CPU starts prepared the next one from Flash.

## 4. Environment Stabilization
To ensure the build system remains stable on Windows:
- **Strict ASCII Policy**: All source files and filenames are kept to the ASCII character set to avoid 'gbk' vs 'utf-8' decode failures in `ninja` and `esptool`.
- **UTF-8 Enforcement**: The `$env:PYTHONUTF8=1` environment variable forces Python scripts to bypass system locale settings.

## 5. Maintenance
When adding new cards:
1. Place JPEG files in the source assets folder.
2. Run `python prepare_assets.py`.
3. Re-flash the SPIFFS image (`idf.py flash`).
