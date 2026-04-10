# 这是一个用于配置 Windows 系统上的 ESP32 Rust 开发环境的 PowerShell 脚本。

Write-Host "开始配置 ESP32 Rust 开发环境..." -ForegroundColor Green

# 1. 检查 Rustup 是否安装
if (-not (Get-Command "cargo" -ErrorAction SilentlyContinue)) {
    Write-Host "错误: 找不到 cargo。请先访问 https://rustup.rs/ 下载并安装 Rust (建议安装包含 MSVC C++ Build Tools 的默认选项)。" -ForegroundColor Red
    Write-Host "安装完成后，请重新打开 PowerShell 并再次运行此脚本。" -ForegroundColor Yellow
    exit
}

Write-Host "Rust 已安装，开始安装 ESP 专属构建和烧录工具..." -ForegroundColor Cyan

# 2. 安装基础工具
# 安装 espup (Espressif 官方的 Rust 工具链安装器)
Write-Host "==== 安装 espup ====" -ForegroundColor Cyan
cargo install espup

# 安装 espflash 和 cargo-espflash (用于烧录程序到 ESP32)
Write-Host "==== 安装 espflash 和 cargo-espflash ====" -ForegroundColor Cyan
cargo install espflash cargo-espflash

# 安装项目生成工具 (用于创建标准模板)
Write-Host "==== 安装 cargo-generate 和 esp-generate ====" -ForegroundColor Cyan
cargo install cargo-generate esp-generate

# 3. 执行 ESP 工具链安装
Write-Host "==== 正在安装 ESP32 Rust 工具链 (这可能需要一些时间，并且会下载几个 G 的数据) ====" -ForegroundColor Cyan
espup install

Write-Host "ESP32 Rust 环境配置完成！" -ForegroundColor Green
Write-Host "注意: espup 安装可能需要在环境变量中追加路径。请根据上方 espup 成功后的提示进行配置，或直接重启终端。" -ForegroundColor Yellow

Write-Host ""
Write-Host "你可以使用以下命令来生成一个新项目：" -ForegroundColor White
Write-Host "  单片机环境 (no_std): esp-generate --chip esp32 项目名" -ForegroundColor DarkGray
Write-Host "  标准库环境 (std): cargo generate esp-rs/esp-idf-template cargo" -ForegroundColor DarkGray
