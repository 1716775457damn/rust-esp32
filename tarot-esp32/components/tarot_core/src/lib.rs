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

    // 2. 初始化 HTTP 服务器
    let mut server = EspHttpServer::new(&HttpConfiguration::default())?;

    // 使用 fn_handler() 而非 handler() —— 这是 esp-idf-svc 官方示例的正确写法
    // fn_handler 会自动处理生命周期泛化，不会触发 HRTB 冲突
    server.fn_handler("/", Method::Get, |req| {
        req.into_ok_response().unwrap()
            .write_all(b"<html><body><h1>Tarot Tarot!</h1><p>Welcome to the Offline Tarot Machine.</p></body></html>").unwrap();
        Ok::<(), core::convert::Infallible>(())
    })?;

    // 将服务器保存到静态变量中防止被 Drop
    unsafe {
        SERVER = Some(server);
    }

    // 将 wifi 泄露以保证后台持续运行不被 Drop
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
