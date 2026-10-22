#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod models;

use std::fs::{self, OpenOptions};
use std::io::Write;
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use tauri::{AppHandle, Manager};
use tauri_plugin_shell::process::{CommandChild, CommandEvent};
use tauri_plugin_shell::ShellExt;

struct ServerChild(Mutex<Option<CommandChild>>);

fn free_port() -> Option<u16> {
    TcpListener::bind("127.0.0.1:0").ok().and_then(|l| l.local_addr().ok()).map(|a| a.port())
}

#[tauri::command]
fn models_status() -> models::Status {
    models::status()
}

#[tauri::command]
async fn download_models(app: AppHandle) -> Result<(), String> {
    models::download_all(&app).await
}

#[tauri::command]
async fn start_server(app: AppHandle) -> Result<String, String> {
    tauri::async_runtime::spawn_blocking(move || start_server_impl(&app))
        .await
        .map_err(|e| e.to_string())?
}

fn sidecar_log_path() -> PathBuf {
    let models = models::models_dir();
    models
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or(models)
        .join("logs")
        .join("sidecar.log")
}

fn spawn_log_reader(
    mut rx: tauri::async_runtime::Receiver<CommandEvent>,
    log_path: PathBuf,
) -> Arc<AtomicBool> {
    let exited = Arc::new(AtomicBool::new(false));
    let exited_flag = Arc::clone(&exited);
    std::thread::spawn(move || {
        let dir = log_path.parent().unwrap_or_else(|| Path::new("."));
        let _ = fs::create_dir_all(dir);
        let rotate = fs::metadata(&log_path).map(|m| m.len() > 1_000_000).unwrap_or(false);
        let mut file = if rotate {
            fs::File::create(&log_path)
        } else {
            OpenOptions::new().create(true).append(true).open(&log_path)
        }
        .ok();
        if let Some(f) = file.as_mut() {
            let _ = writeln!(f, "=== {} ===", log_path.display());
        }
        while let Some(event) = rx.blocking_recv() {
            match event {
                CommandEvent::Stdout(bytes) => {
                    if let Some(f) = file.as_mut() {
                        let _ = writeln!(f, "[out] {}", String::from_utf8_lossy(&bytes));
                    }
                }
                CommandEvent::Stderr(bytes) => {
                    if let Some(f) = file.as_mut() {
                        let _ = writeln!(f, "[err] {}", String::from_utf8_lossy(&bytes));
                    }
                }
                CommandEvent::Error(err) => {
                    if let Some(f) = file.as_mut() {
                        let _ = writeln!(f, "[error] {err}");
                    }
                }
                CommandEvent::Terminated(payload) => {
                    if let Some(f) = file.as_mut() {
                        let _ = writeln!(
                            f,
                            "[exit] code={:?} signal={:?}",
                            payload.code, payload.signal
                        );
                    }
                    exited_flag.store(true, Ordering::SeqCst);
                }
                _ => {}
            }
        }
        if let Some(mut f) = file {
            let _ = f.flush();
        }
    });
    exited
}

fn stop_server(app: &AppHandle) {
    let child = app.state::<ServerChild>().0.lock().unwrap().take();
    if let Some(child) = child {
        let _ = child.kill();
    }
}

fn fail_with_log(app: &AppHandle, log_path: &Path, message: &str) -> String {
    stop_server(app);
    format!("{message}. Лог: {}", log_path.display())
}

fn start_server_impl(app: &AppHandle) -> Result<String, String> {
    let port = match std::env::var("HOLOS_PORT") {
        Ok(p) if !p.is_empty() => p.parse().map_err(|_| "некоректний HOLOS_PORT")?,
        _ => free_port().ok_or("не вдалося знайти вільний порт")?,
    };
    let mdir = models::models_dir();
    let sdir = mdir.parent().map(|p| p.join("speakers")).unwrap_or_else(|| PathBuf::from("speakers"));

    let mut cmd = app
        .shell()
        .sidecar("holos-server")
        .map_err(|e| format!("sidecar holos-server не знайдено: {e}"))?
        .env("HOLOS_HOST", "127.0.0.1")
        .env("HOLOS_PORT", port.to_string())
        .env("HOLOS_MODELS_DIR", &mdir)
        .env("HOLOS_VERBALIZER_DIR", mdir.join("verbalizer").join("gemma-int8"))
        .env("HOLOS_VOICE_ENCODER", mdir.join("voice_encoder.onnx"))
        .env("HOLOS_SPEAKERS_DIR", &sdir);

    let res = app.path().resource_dir().map_err(|e| e.to_string())?;
    let lib = res.join("lib");
    #[cfg(target_os = "linux")]
    {
        let mut ld = lib.display().to_string();
        if let Ok(old) = std::env::var("LD_LIBRARY_PATH") {
            if !old.is_empty() {
                ld.push(':');
                ld.push_str(&old);
            }
        }
        cmd = cmd.env("LD_LIBRARY_PATH", ld);
    }
    #[cfg(target_os = "windows")]
    {
        // ORT DLLs live next to the executables; keep lib/ as a fallback for
        // installs created before the layout change.
        let mut path = res.display().to_string();
        if lib.is_dir() {
            path.push(';');
            path.push_str(&lib.display().to_string());
        }
        if let Ok(old) = std::env::var("PATH") {
            if !old.is_empty() {
                path.push(';');
                path.push_str(&old);
            }
        }
        cmd = cmd.env("PATH", path);
    }
    #[cfg(not(any(target_os = "linux", target_os = "windows")))]
    let _ = lib;

    let (rx, child) = cmd.spawn().map_err(|e| format!("не вдалося запустити сервер: {e}"))?;
    let log_path = sidecar_log_path();
    let exited = spawn_log_reader(rx, log_path.clone());
    *app.state::<ServerChild>().0.lock().unwrap() = Some(child);

    let base = format!("http://127.0.0.1:{port}");
    let client = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(2))
        .build()
        .map_err(|e| e.to_string())?;

    for _ in 0..240 {
        if exited.load(Ordering::SeqCst) {
            return Err(fail_with_log(app, &log_path, "сервер завершився під час запуску"));
        }
        if let Ok(r) = client.get(format!("{base}/healthz")).send() {
            if r.status().is_success() {
                return Ok(base);
            }
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    Err(fail_with_log(
        app,
        &log_path,
        "сервер не відповідає на /healthz за 120 c",
    ))
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .manage(ServerChild(Mutex::new(None)))
        .invoke_handler(tauri::generate_handler![models_status, download_models, start_server])
        .build(tauri::generate_context!())
        .expect("error while running tauri application")
        .run(|app, event| {
            if let tauri::RunEvent::ExitRequested { .. } = event {
                stop_server(app);
            }
        });
}
