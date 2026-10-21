#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod models;

use std::net::TcpListener;
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::Duration;

use tauri::{AppHandle, Manager, State};
use tauri_plugin_shell::process::Child;
use tauri_plugin_shell::ShellExt;

struct ServerChild(Mutex<Option<Child>>);

fn free_port() -> Option<u16> {
    TcpListener::bind("127.0.0.1:0").ok().and_then(|l| l.local_addr().ok()).map(|a| a.port())
}

#[tauri::command]
fn models_status() -> models::Status {
    models::status()
}

#[tauri::command]
async fn download_models(app: AppHandle) -> Result<(), String> {
    let app2 = app.clone();
    tauri::async_runtime::spawn_blocking(move || models::download_all(&app2))
        .await
        .map_err(|e| e.to_string())?
}

#[tauri::command]
fn start_server(app: AppHandle, state: State<'_, ServerChild>) -> Result<String, String> {
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
        let mut path = lib.display().to_string();
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

    let (_rx, child) = cmd.spawn().map_err(|e| format!("не вдалося запустити сервер: {e}"))?;
    *state.0.lock().unwrap() = Some(child);

    let base = format!("http://127.0.0.1:{port}");
    let client = reqwest::blocking::Client::builder()
        .timeout(Duration::from_secs(2))
        .build()
        .map_err(|e| e.to_string())?;
    for _ in 0..240 {
        if let Ok(r) = client.get(format!("{base}/healthz")).send() {
            if r.status().is_success() {
                return Ok(base);
            }
        }
        std::thread::sleep(Duration::from_millis(500));
    }
    Err("сервер не відповідає на /healthz за 120 c".into())
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
                let state = app.state::<ServerChild>();
                if let Some(mut child) = state.0.lock().unwrap().take() {
                    let _ = child.kill();
                }
            }
        });
}
