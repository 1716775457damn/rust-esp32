use anyhow::Result;
use embedded_svc::http::Method;
use embedded_svc::io::Write;
use esp_idf_svc::eventloop::EspSystemEventLoop;
// use esp_idf_svc::http::server::Configuration as HttpConfiguration;
use esp_idf_svc::http::server::EspHttpServer;
use esp_idf_svc::wifi::{AccessPointConfiguration, Configuration, EspWifi};
use log::info;

// 保证服务器对象不被销毁
static mut SERVER: Option<EspHttpServer<'static>> = None;

/// Phase 2: 启动 AP 热点和 HTTP 服务器
#[no_mangle]
pub extern "C" fn rust_start_ap_server() {
    esp_idf_svc::log::EspLogger::initialize_default();
    info!("正在启动 Rust AP Server...");

    if let Err(e) = start_services() {
        info!("启动失败: {:?}", e);
    }
}

fn start_services() -> Result<()> {
    let peripherals = esp_idf_hal::peripherals::Peripherals::take()?;
    let sys_loop = EspSystemEventLoop::take()?;
    let nvs = esp_idf_svc::nvs::EspDefaultNvsPartition::take()?;

    // 0. 初始化 SPIFFS 用于存储图片 (Phase 3)
    let spiffs_conf = esp_idf_sys::esp_vfs_spiffs_conf_t {
        base_path: b"/spiffs\0".as_ptr() as *const u8,
        partition_label: core::ptr::null(), // 使用默认的 spiffs label
        max_files: 5,
        format_if_mount_failed: true,
    };
    // 使用 FFI 强行挂载文件系统
    unsafe {
        esp_idf_sys::esp!(esp_idf_sys::esp_vfs_spiffs_register(&spiffs_conf))?;
    }
    info!("SPIFFS 已成功挂载在 /spiffs");

    // 1. 初始化 Wi-Fi AP
    let mut wifi = EspWifi::new(
        peripherals.modem,
        sys_loop,
        Some(nvs),
    )?;

    wifi.set_configuration(&Configuration::AccessPoint(AccessPointConfiguration {
        ssid: "TarotCard".try_into().unwrap(),
        ssid_hidden: false,
        auth_method: embedded_svc::wifi::AuthMethod::None,
        channel: 6,
        ..Default::default()
    }))?;

    wifi.start()?;
    info!("Wi-Fi AP 已开启: SSID = TarotCard");

    // 2. 初始化 HTTP 服务器 (增大栈空间至 16KB 以支持图像解码)
    let server_config = esp_idf_svc::http::server::Configuration {
        stack_size: 16384,
        ..Default::default()
    };
    let mut server = EspHttpServer::new(&server_config)?;
    info!("✅ HTTP 服务器配置完成 (Stack: 16KB)");

    // Root / : 提供华丽的 Web UI
    server.fn_handler("/", Method::Get, |req| {
        const INDEX_HTML: &str = r##"
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Tarot Card Machine</title>
            <style>
                body {
                    background: radial-gradient(circle at center, #1a0b2e 0%, #0d0415 100%);
                    color: #e0d0ff;
                    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                    display: flex;
                    flex-direction: column;
                    align-items: center;
                    justify-content: center;
                    height: 100vh;
                    margin: 0;
                    overflow: hidden;
                }
                .container {
                    text-align: center;
                    padding: 40px;
                    background: rgba(255, 255, 255, 0.05);
                    backdrop-filter: blur(10px);
                    border-radius: 30px;
                    border: 1px solid rgba(255, 255, 255, 0.1);
                    box-shadow: 0 20px 50px rgba(0, 0, 0, 0.5);
                }
                h1 {
                    font-weight: 300;
                    letter-spacing: 4px;
                    text-transform: uppercase;
                    margin-bottom: 30px;
                    background: linear-gradient(45deg, #ffd700, #ff8c00);
                    -webkit-background-clip: text;
                    -webkit-text-fill-color: transparent;
                }
                button {
                    padding: 20px 40px;
                    font-size: 1.2rem;
                    background: linear-gradient(135deg, #6b4c9a 0%, #4a2c7a 100%);
                    color: white;
                    border: none;
                    border-radius: 50px;
                    cursor: pointer;
                    transition: all 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
                    box-shadow: 0 10px 20px rgba(107, 76, 154, 0.3);
                    text-transform: uppercase;
                    letter-spacing: 2px;
                }
                button:hover {
                    transform: scale(1.05) translateY(-5px);
                    box-shadow: 0 15px 30px rgba(107, 76, 154, 0.5);
                }
                button:active {
                    transform: scale(0.95);
                }
                #status {
                    margin-top: 25px;
                    font-size: 0.95rem;
                    font-weight: 300;
                    letter-spacing: 1px;
                    min-height: 20px;
                }
            </style>
        </head>
        <body>
            <div class="container">
                <h1>Tarot Machine</h1>
                <button onclick="drawCard()">Draw a Card</button>
                <div id="status">Ready to see your destiny...</div>
            </div>
            <script>
                function drawCard() {
                    const btn = document.querySelector('button');
                    const status = document.getElementById('status');
                    btn.disabled = true;
                    status.style.color = "#ffd700";
                    status.innerText = "Consulting the stars...";
                    
                    fetch('/draw')
                        .then(r => r.text())
                        .then(t => {
                            status.style.color = "#00ffcc";
                            status.innerText = "Your card is revealed on the device!";
                            setTimeout(() => {
                                btn.disabled = false;
                                status.style.color = "#e0d0ff";
                                status.innerText = "Ready for another draw.";
                            }, 4000);
                        })
                        .catch(e => {
                            status.style.color = "#ff4d4d";
                            status.innerText = "Connection lost. Try again.";
                            btn.disabled = false;
                        });
                }
            </script>
        </body>
        </html>
        "##;
        let mut response = req.into_ok_response().map_err(|e| anyhow::anyhow!("Response error: {:?}", e))?;
        response.write_all(INDEX_HTML.as_bytes()).map_err(|e| anyhow::anyhow!("Write error: {:?}", e))?;
        Ok::<(), anyhow::Error>(())
    })?;

    // GET /draw : 随机抽牌并解码显示
    server.fn_handler("/draw", Method::Get, |req| {
        info!("收到随机抽牌指令...");
        
        // 1. 生成随机索引 (0-94)
        let random_idx = unsafe { esp_idf_sys::esp_random() % 95 };
        let jpg_path = format!("/spiffs/{}.jpg", random_idx);
        
        info!("选中卡片: {}", jpg_path);
        
        // 2. 读取本地 JPEG
        let jpeg_data = match std::fs::read(&jpg_path) {
            Ok(data) => data,
            Err(e) => {
                info!("无法读取文件 {}: {:?}", jpg_path, e);
                let _ = req.into_status_response(500).and_then(|mut r| r.write_all(b"File Read Error"));
                return Ok::<(), anyhow::Error>(());
            }
        };

        // 3. 执行解码逻辑 (复用 slot 0 作为当前显示槽位)
        if let Err(e) = decode_jpeg_to_rgb565(&jpeg_data, 0) {
            info!("解码失败: {:?}", e);
            let _ = req.into_status_response(500).and_then(|mut r| r.write_all(b"Decode Error"));
        } else {
            info!("✅ 抽牌显示成功 (Index: {})", random_idx);
            let _ = req.into_ok_response().and_then(|mut r| r.write_all(format!("OK:{}", random_idx).as_bytes()));
        }
        
        Ok::<(), anyhow::Error>(())
    })?;

    // POST /upload?slot={id} : 接收二进制图片并写入 SPIFFS (保留作为调试)
    server.fn_handler("/upload", Method::Post, |mut req| {
        // ... (省略部分解析逻辑)
        let mut jpeg_data = Vec::new();
        let mut buf = [0u8; 1024];
        while let Ok(bytes_read) = req.read(&mut buf) {
            if bytes_read == 0 { break; }
            jpeg_data.extend_from_slice(&buf[..bytes_read]);
        }
        
        if let Err(e) = decode_jpeg_to_rgb565(&jpeg_data, 0) {
            let _ = req.into_status_response(500).and_then(|mut r| r.write_all(b"Error"));
        } else {
            let _ = req.into_ok_response().and_then(|mut r| r.write_all(b"OK"));
        }
        Ok::<(), anyhow::Error>(())
    })?;

    // 将服务保存到静态变量中防止被 Drop
    unsafe {
        SERVER = Some(server);
    }
    Box::leak(Box::new(wifi));

    info!("HTTP 服务器已启动，监听端口 80");
    Ok(())
}

/// 核心解码逻辑：将内存中的 JPEG 解码到 SPIFFS 文件
fn decode_jpeg_to_rgb565(jpeg_data: &[u8], slot_id: i32) -> Result<()> {
    use tjpgdec_rs::{JpegDecoder, MemoryPool};
    use std::io::Write;

    let mut pool_buffer = vec![0u8; 4096]; 
    let mut pool = MemoryPool::new(&mut pool_buffer);
    let mut decoder = JpegDecoder::new();

    decoder.prepare(jpeg_data, &mut pool).map_err(|e| anyhow::anyhow!("JPEG Prepare Error: {:?}", e))?;
    
    let width = decoder.width();
    let rgb_path = format!("/spiffs/tarot_{}.rgb565", slot_id); // 统一后缀
    let mut rgb_file = std::fs::File::create(&rgb_path)?;

    let mut line_buffer = vec![0u8; width as usize * 16 * 2];
    let mut current_row_top = 0;
    let mut mcu_buf = vec![0i16; decoder.mcu_buffer_size()];
    let mut work_buf = vec![0u8; decoder.work_buffer_size()];

    let mut write_err = false;
    decoder.decompress(
        jpeg_data,
        0, 
        &mut mcu_buf,
        &mut work_buf,
        &mut |_decoder, bitmap: &[u8], rect| {
            if write_err { return Ok(false); }
            if rect.top != current_row_top {
                rgb_file.write_all(&line_buffer).map_err(|_| { write_err = true; }).ok();
                line_buffer.fill(0);
                current_row_top = rect.top;
            }
            let b_width = rect.right - rect.left + 1;
            let b_height = rect.bottom - rect.top + 1;
            for y in 0..b_height {
                for x in 0..b_width {
                    let src_idx = (y as usize * b_width as usize + x as usize) * 3;
                    let p = ((bitmap[src_idx] as u16 & 0xF8) << 8) | ((bitmap[src_idx + 1] as u16 & 0xFC) << 3) | (bitmap[src_idx + 2] as u16 >> 3);
                    let dest_idx = ((rect.top + y - current_row_top) as usize * width as usize + (rect.left + x) as usize) * 2;
                    line_buffer[dest_idx] = (p >> 8) as u8;
                    line_buffer[dest_idx + 1] = (p & 0xFF) as u8;
                }
            }
            Ok(true)
        }
    ).map_err(|e| anyhow::anyhow!("Decode Decompress Error: {:?}", e))?;

    rgb_file.write_all(&line_buffer)?;
    
    // 通知 C++
    extern "C" { fn cpp_notify_card_ready(slot_id: i32); }
    unsafe { cpp_notify_card_ready(slot_id); }
    
    Ok(())
}

/// Phase 1 遗留：返回 Rust 组件版本
#[no_mangle]
pub extern "C" fn rust_get_version() -> *const u8 {
    let version = b"Tarot_Core_v0.6_LocalDraw\0";
    version.as_ptr()
}
