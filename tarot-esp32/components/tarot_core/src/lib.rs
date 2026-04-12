use anyhow::Result;
use embedded_svc::http::Method;
use embedded_svc::io::Write;
use esp_idf_svc::eventloop::EspSystemEventLoop;
// use esp_idf_svc::http::server::Configuration as HttpConfiguration;
use esp_idf_svc::http::server::EspHttpServer;
use esp_idf_svc::wifi::{AccessPointConfiguration, Configuration, EspWifi};
use log::info;

// Keep server alive
static mut SERVER: Option<EspHttpServer<'static>> = None;

// Metadata Cache (Performance Optimization)
static CARD_METADATA_CACHE: std::sync::OnceLock<serde_json::Value> = std::sync::OnceLock::new();

// C++ FFI Functions
extern "C" {
    fn cpp_ui_start_shuffle();
    fn cpp_ui_display_info(name: *const std::os::raw::c_char, keywords: *const std::os::raw::c_char);
    fn cpp_notify_card_ready(slot_id: i32);
    fn rust_play_sound(sound_type: *const std::os::raw::c_char);
}

/// Phase 2: 启动 AP 热点和 HTTP 服务器
#[no_mangle]
pub extern "C" fn rust_start_ap_server() {
    esp_idf_svc::log::EspLogger::initialize_default();
    info!("Starting Rust AP Server...");

    if let Err(e) = start_services() {
        info!("Start failed: {:?}", e);
    }
}

fn start_services() -> Result<()> {
    let peripherals = esp_idf_hal::peripherals::Peripherals::take()?;
    let sys_loop = EspSystemEventLoop::take()?;
    let nvs = esp_idf_svc::nvs::EspDefaultNvsPartition::take()?;

    // 0. Mount SPIFFS for card assets
    let spiffs_conf = esp_idf_sys::esp_vfs_spiffs_conf_t {
        base_path: b"/spiffs\0".as_ptr() as *const u8,
        partition_label: core::ptr::null(), // Use default label
        max_files: 5,
        format_if_mount_failed: true,
    };
    // Force mount using FFI
    unsafe {
        esp_idf_sys::esp!(esp_idf_sys::esp_vfs_spiffs_register(&spiffs_conf))?;
    }
    info!("SPIFFS mounted successfully at /spiffs");

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
    info!("Wi-Fi AP started: SSID = TarotCard");

    // 2. Init HTTP Server (20KB stack for image processes)
    let server_config = esp_idf_svc::http::server::Configuration {
        stack_size: 20480, 
        ..Default::default()
    };
    let mut server = EspHttpServer::new(&server_config)?;
    info!("✅ HTTP Server initialized (Stack: 20KB)");

    // Root / : 提供华丽的 Web UI
    server.fn_handler("/", Method::Get, |req| {
        const INDEX_HTML: &str = r##"
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Celestial Tarot Machine</title>
            <style>
                :root { --gold: #ffd700; --mystic-purple: #2a1b4d; --accent: #ffd700; }
                body {
                    background: radial-gradient(circle at center, #1a0b2e 0%, #050208 100%);
                    color: white; font-family: 'Cinzel', serif;
                    display: flex; flex-direction: column; align-items: center; justify-content: center;
                    height: 100vh; margin: 0; overflow: hidden;
                }
                .ritual-circle {
                    position: relative; width: 320px; height: 320px;
                    border: 2px dashed rgba(255, 215, 0, 0.2);
                    border-radius: 50%; display: flex; align-items: center; justify-content: center;
                    animation: rotate 60s linear infinite;
                }
                @keyframes rotate { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
                .ui-stable {
                    position: absolute; width: 100%; height: 100%;
                    display: flex; flex-direction: column; align-items: center; justify-content: center;
                    transform: rotate(0deg); /* Counter-rotate if needed */
                }
                .card-viewport {
                    perspective: 1000px; width: 140px; height: 220px;
                    margin-bottom: 20px;
                }
                .card-inner {
                    position: relative; width: 100%; height: 100%;
                    text-align: center; transition: transform 1.2s cubic-bezier(0.4, 0, 0.2, 1);
                    transform-style: preserve-3d;
                }
                .card-inner.flipped { transform: rotateY(180deg); }
                .card-face {
                    position: absolute; width: 100%; height: 100%;
                    backface-visibility: hidden; border-radius: 8px;
                    box-shadow: 0 0 15px rgba(255, 215, 0, 0.3);
                }
                .card-back {
                    background: linear-gradient(135deg, #2a1b4d 0%, #1a0b2e 100%);
                    border: 2px solid var(--gold);
                    display: flex; align-items: center; justify-content: center;
                }
                .card-front {
                    background: white; color: black; transform: rotateY(180deg);
                    display: flex; align-items: center; justify-content: center;
                    border: 4px solid var(--gold);
                }
                .card-content { font-size: 2.5rem; }
                button {
                    background: transparent; border: 2px solid var(--gold);
                    color: var(--gold); padding: 12px 30px; border-radius: 30px;
                    text-transform: uppercase; letter-spacing: 3px; cursor: pointer;
                    transition: 0.3s; z-index: 100;
                }
                button:hover { background: var(--gold); color: black; box-shadow: 0 0 20px var(--gold); }
                button:disabled { border-color: #444; color: #444; cursor: not-allowed; }
                #result-text {
                    margin-top: 20px; text-align: center; opacity: 0; transition: 1s;
                }
                .reveal { opacity: 1 !important; }
                h2 { color: var(--gold); margin: 0; font-weight: 300; letter-spacing: 2px; }
            </style>
        </head>
        <body>
            <div class="ritual-circle"></div>
            <div class="ui-stable" id="mainUI">
                <div class="card-viewport">
                    <div class="card-inner" id="card">
                        <div class="card-face card-back">✨</div>
                        <div class="card-face card-front"><div class="card-content">🎴</div></div>
                    </div>
                </div>
                <button onclick="drawCard()" id="drawBtn">Consult Destiny</button>
                <div id="result-text">
                    <h2 id="cardName">The Moon</h2>
                    <p style="color: #aaa; font-size: 0.8rem;">Observe the ritual on your device</p>
                </div>
            </div>
            <script>
                function drawCard() {
                    const btn = document.getElementById('drawBtn');
                    const card = document.getElementById('card');
                    const result = document.getElementById('result-text');
                    const cardName = document.getElementById('cardName');
                    
                    btn.disabled = true;
                    card.classList.remove('flipped');
                    result.classList.remove('reveal');
                    
                    fetch('/draw')
                        .then(r => r.json())
                        .then(data => {
                            setTimeout(() => {
                                card.classList.add('flipped');
                                cardName.innerText = data.name;
                                result.classList.add('reveal');
                                btn.disabled = false;
                            }, 1000); // Sync with shuffle animation
                        })
                        .catch(e => {
                            alert("The ritual was interrupted.");
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

    // GET /draw : Random draw with ritual enhancements
    server.fn_handler("/draw", Method::Get, |req| {
        info!("Received draw command...");
        
        // --- Ritual Phase 1: Start shuffle animation ---
        unsafe {
            cpp_ui_start_shuffle();
            rust_play_sound(std::ffi::CString::new("shuffle").unwrap().as_ptr());
        }
        
        // 1. Generate random index (0-77)
        let random_idx = unsafe { esp_idf_sys::esp_random() % 78 };
        
        // --- Ritual Phase 2: Fetch metadata and notify C++ for direct HW draw ---
        let (card_name, card_keywords) = get_card_metadata_en(random_idx as i32);
        unsafe {
            let c_name = std::ffi::CString::new(card_name.clone()).unwrap();
            let c_keys = std::ffi::CString::new(card_keywords.clone()).unwrap();
            cpp_ui_display_info(c_name.as_ptr(), c_keys.as_ptr());
            
            // Phase 3: Immediate transition to hardware render
            rust_play_sound(std::ffi::CString::new("draw").unwrap().as_ptr());
            cpp_notify_card_ready(random_idx as i32); 
        }

        info!("Selected Card Index: {}", random_idx);
        
        let json_resp = format!(r#"{{"index": {}, "name": "{}"}}"#, random_idx, card_name);
        let mut resp = req.into_response(200, None, &[("Content-Type", "application/json")])?;
        resp.write_all(json_resp.as_bytes())?;
        
        Ok::<(), anyhow::Error>(())
    })?;

    // POST /upload?slot={id} : Legacy placeholder
    server.fn_handler("/upload", Method::Post, |mut _req| {
        let _ = _req.into_ok_response().and_then(|mut r| r.write_all(b"OK"));
        Ok::<(), anyhow::Error>(())
    })?;

    // Leak references to keep them alive
    unsafe {
        SERVER = Some(server);
    }
    Box::leak(Box::new(wifi));

    info!("HTTP Server started on port 80");
    Ok(())
}

/// Returns Rust component version
#[no_mangle]
pub extern "C" fn rust_get_version() -> *const u8 {
    let version = b"Tarot_Core_v0.8_HighSpeed_RGB565\0";
    version.as_ptr()
}

fn get_card_metadata_en(idx: i32) -> (String, String) {
    let metadata = CARD_METADATA_CACHE.get_or_init(|| {
        if let Ok(data) = std::fs::read_to_string("/spiffs/meanings_en.json") {
            if let Ok(v) = serde_json::from_str::<serde_json::Value>(&data) {
                return v;
            }
        }
        serde_json::Value::Object(serde_json::Map::new())
    });

    if let Some(card) = metadata.get(idx.to_string()) {
        let name = card.get("n").and_then(|n| n.as_str()).unwrap_or("Unknown").to_string();
        let keys = card.get("k").and_then(|k| k.as_str()).unwrap_or("").to_string();
        return (name, keys);
    }
    ("Unknown Card".to_string(), "".to_string())
}

fn get_card_name(idx: i32) -> String {
    get_card_metadata_en(idx).0
}
