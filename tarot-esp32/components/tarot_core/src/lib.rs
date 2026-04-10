use anyhow::Result;
use embedded_svc::http::Method;
use embedded_svc::io::Write;
use esp_idf_svc::eventloop::EspSystemEventLoop;
use esp_idf_svc::http::server::Configuration as HttpConfiguration;
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

    // GET / : 返回前端上传网页
    server.fn_handler("/", Method::Get, |req| {
        let html = include_str!("html/index.html");
        let _ = req.into_ok_response().and_then(|mut r| r.write_all(html.as_bytes()));
        Ok::<(), core::convert::Infallible>(())
    })?;

    // POST /upload?slot={id} : 接收二进制图片并写入 SPIFFS
    server.fn_handler("/upload", Method::Post, |mut req| {
        // 解析 slot id
        let query = req.uri().split('?').nth(1).unwrap_or("");
        let mut slot = 99;
        for param in query.split('&') {
            if let Some((k, v)) = param.split_once('=') {
                if k == "slot" {
                    if let Ok(id) = v.parse::<u8>() {
                        slot = id;
                    }
                }
            }
        }

        if slot > 21 {
            let _ = req.into_status_response(400).and_then(|mut r| r.write_all(b"Invalid slot ID"));
            return Ok::<(), core::convert::Infallible>(());
        }

        info!("正在接收 Slot {} 的图片数据...", slot);

        // 1. 将 JPEG 数据先读入内存 (约 15-30KB)
        let mut jpeg_data = Vec::new();
        let mut buf = [0u8; 1024];
        while let Ok(bytes_read) = req.read(&mut buf) {
            if bytes_read == 0 { break; }
            jpeg_data.extend_from_slice(&buf[..bytes_read]);
        }

        if jpeg_data.is_empty() {
            let _ = req.into_status_response(400).and_then(|mut r| r.write_all(b"Empty data"));
            return Ok::<(), core::convert::Infallible>(());
        }

        // 2. 准备输出文件 (.rgb 原始数据)
        let rgb_path = format!("/spiffs/card_{}.rgb", slot);
        let mut rgb_file = match std::fs::File::create(&rgb_path) {
            Ok(f) => f,
            Err(e) => {
                info!("无法创建文件 {:?}: {:?}", rgb_path, e);
                let _ = req.into_status_response(500).and_then(|mut r| r.write_all(b"FS Create Error"));
                return Ok::<(), core::convert::Infallible>(());
            }
        };

        // 3. 使用 tjpgdec_rs 逐块解码并写入
        use tjpgdec_rs::{JpegDecoder, MemoryPool, RECOMMENDED_POOL_SIZE};
        // 将 pool_buffer 移至堆中 (Vec)，增加到 4096 字节以应对 360x360 图像
        let mut pool_buffer = vec![0u8; 4096]; 
        let mut pool = MemoryPool::new(&mut pool_buffer);
        let mut decoder = JpegDecoder::new();

        info!("开始解码 JPEG 并转换为 RGB565...");
        if let Err(e) = decoder.prepare(&jpeg_data, &mut pool) {
            info!("JPEG 预处理失败: {:?}", e);
            let _ = req.into_status_response(400).and_then(|mut r| r.write_all(b"Invalid JPEG"));
            return Ok::<(), core::convert::Infallible>(());
        };
        
        let width = decoder.width();
        let height = decoder.height();
        info!("图片尺寸: {}x{}", width, height);

        // 4. 优化写入策略：使用 16 行宽的行缓冲区 (360 * 16 * 2 = 11.5KB)
        // 这样可以将随机的 MCU 块写入转换为顺序的文件写入，极大提升 SPIFFS 性能并解决 I/O Error
        let mut line_buffer = vec![0u8; width as usize * 16 * 2];
        let mut current_row_top = 0;
        let mut mcu_buf = vec![0i16; decoder.mcu_buffer_size()];
        let mut work_buf = vec![0u8; decoder.work_buffer_size()];

        use std::io::Write;
        let mut write_err = false;
        let decode_res = decoder.decompress(
            &jpeg_data,
            0, // scale: 1/1
            &mut mcu_buf,
            &mut work_buf,
            &mut |_decoder, bitmap: &[u8], rect| {
                if write_err { return Ok(false); }

                // 如果进入了下一排 MCU (top 坐标改变)，先冲刷当前行缓冲区到文件
                if rect.top != current_row_top {
                    if let Err(e) = rgb_file.write_all(&line_buffer) {
                        info!("❌ SPIFFS 顺序写入失败: {:?}", e);
                        write_err = true;
                        return Ok(false);
                    }
                    line_buffer.fill(0);
                    current_row_top = rect.top;
                }

                // 将当前 MCU 块的数据填入行缓冲区
                // bitmap 包含当前矩形的 RGB888 数据，大小为 rect.width * rect.height * 3
                let b_width = rect.right - rect.left + 1;
                let b_height = rect.bottom - rect.top + 1;
                
                for y in 0..b_height {
                    for x in 0..b_width {
                        let src_idx = (y as usize * b_width as usize + x as usize) * 3;
                        let r = bitmap[src_idx] as u16;
                        let g = bitmap[src_idx + 1] as u16;
                        let b = bitmap[src_idx + 2] as u16;
                        
                        // RGB888 -> RGB565 (Big Endian)
                        let p = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
                        
                        // 计算在 line_buffer 中的位置
                        // line_buffer 存储了 16 行的数据，当前行是 (rect.top + y - current_row_top)
                        let local_y = rect.top + y - current_row_top;
                        let dest_idx = (local_y as usize * width as usize + (rect.left + x) as usize) * 2;
                        
                        line_buffer[dest_idx] = (p >> 8) as u8;
                        line_buffer[dest_idx + 1] = (p & 0xFF) as u8;
                    }
                }
                Ok(true)
            }
        );

        // 冲刷最后残余的数据
        if !write_err && decode_res.is_ok() {
            let _ = rgb_file.write_all(&line_buffer);
        }

        if let Err(e) = decode_res {
            info!("❌ 解码过程出错: {:?}", e);
            let _ = req.into_status_response(500).and_then(|mut r| r.write_all(format!("Decode Error: {:?}", e).as_bytes()));
        } else if write_err {
            info!("❌ 写入 SPIFFS 出错 (WriteErr flag set)");
            let _ = req.into_status_response(500).and_then(|mut r| r.write_all(b"File Write Error"));
        } else {
            info!("✅ Slot {} 图片保存成功! 路径: {:?}", slot, rgb_path);
            let _ = req.into_ok_response().and_then(|mut r| r.write_all(b"OK"));
        }
        
        Ok::<(), core::convert::Infallible>(())
    })?;

    // 将服务保存到静态变量中防止被 Drop
    unsafe {
        SERVER = Some(server);
    }
    Box::leak(Box::new(wifi));

    info!("HTTP 服务器已启动，监听端口 80");
    Ok(())
}

/// Phase 1 遗留：返回 Rust 组件版本
#[no_mangle]
pub extern "C" fn rust_get_version() -> *const u8 {
    let version = b"Tarot_Core_v0.2_Phase_2\0";
    version.as_ptr()
}
