# Tarot ESP32 UI 3.0: High-Speed Ritual Engine

A professional Tarot-drawing firmware for ESP32-C3 with a circular display, featuring a hybrid Rust/C++ architecture for optimal safety and performance.

## 🌟 Core Features
- **Instant Revelation**: Optimized RGB565 pipeline enables <100ms card rendering.
- **Hybrid Core**: Logic and metadata handled by **Rust**, hardware rendering handled by **C++**.
- **Ritual Engine**: Asynchronous task scheduling for smooth UI transitions and audio synchronisation.
- **78-Card Standard**: Fully supports the standard Tarot deck with numeric prefix mapping.

## 🚀 Performance Architecture
The project utilizes a **Build-time Pre-processing** strategy to bypass the limitations of embedded CPUs. 
- For more details, see [DOCS/RGB565_PIPELINE.md](DOCS/RGB565_PIPELINE.md)

## 🛠️ Getting Started

### Prerequisites
- ESP-IDF v5.3.1
- Rust (riscv32imc-esp-espidf)
- Python with Pillow library

### Build & Flash
On Windows, ensure you set the UTF-8 environment variable to avoid build errors:
```powershell
# 1. Prepare assets (converts JPG to RGB565 BIN)
python prepare_assets.py

# 2. Build and Flash
$env:PYTHONUTF8=1
idf.py -p [YOUR_PORT] build flash monitor
```

## 📂 Project Structure
- `main/`: C++ Hardware drivers and Ritual UI logic.
- `components/tarot_core/`: Rust logic core (FFI-bridged).
- `spiffs_data/`: High-performance assets (.bin) and metadata.
- `prepare_assets.py`: Offline asset conversion pipeline.

---
*Note: This project is optimized for performance and stability in Windows development environments.*
