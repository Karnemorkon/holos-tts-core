use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use serde::Serialize;
use tauri::{AppHandle, Emitter};

const HF_BASE: &str = "https://huggingface.co/Morkon/holos-native-models/resolve/main";
const MAX_ATTEMPTS: u32 = 3;
const PROGRESS_INTERVAL: Duration = Duration::from_millis(100);

// (HF path, relative dest) — дзеркало packaging/linux/download-models.sh
const FILES: &[(&str, &str)] = &[
    ("stress/stress.trie", "stress.trie"),
    ("tts/vocab.json", "vocab.json"),
    ("tts/voices.json", "voices.json"),
    ("tts/holos_cpu_int8.onnx", "holos_cpu_int8.onnx"),
    ("tts/voice_encoder.onnx", "voice_encoder.onnx"),
    ("accentor/accentor-lite.onnx", "accentor-lite.onnx"),
    ("verbalizer/gemma-int8/model.onnx", "verbalizer/gemma-int8/model.onnx"),
    ("verbalizer/gemma-int8/tokenizer.model", "verbalizer/gemma-int8/tokenizer.model"),
    ("verbalizer/gemma-int8/config.json", "verbalizer/gemma-int8/config.json"),
    ("verbalizer/gemma-int8/GEMMA-NOTICE.txt", "verbalizer/gemma-int8/GEMMA-NOTICE.txt"),
];

pub fn models_dir() -> PathBuf {
    #[cfg(windows)]
    {
        let base = std::env::var_os("LOCALAPPDATA")
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("."));
        base.join("holos-tts").join("models")
    }
    #[cfg(not(windows))]
    {
        let base = match std::env::var_os("XDG_DATA_HOME") {
            Some(v) if !v.is_empty() => PathBuf::from(v),
            _ => {
                let home = std::env::var_os("HOME").unwrap_or_else(|| ".".into());
                PathBuf::from(home).join(".local").join("share")
            }
        };
        base.join("holos-tts-core").join("models")
    }
}

fn file_size(p: &Path) -> u64 {
    fs::metadata(p).map(|m| m.len()).unwrap_or(0)
}

fn present(p: &Path) -> bool {
    file_size(p) > 0
}

#[derive(Serialize)]
pub struct FileStatus {
    pub name: String,
    pub size: u64,
    pub have: bool,
}

#[derive(Serialize)]
pub struct Status {
    pub models_dir: String,
    pub total_bytes: u64,
    pub have_bytes: u64,
    pub files: Vec<FileStatus>,
}

pub fn status() -> Status {
    let dir = models_dir();
    let mut files = Vec::new();
    let mut total = 0u64;
    let mut have = 0u64;
    for (_, dst) in FILES {
        let p = dir.join(dst);
        let sz = file_size(&p);
        let ok = present(&p);
        total += sz.max(1);
        if ok {
            have += sz;
        }
        files.push(FileStatus { name: dst.to_string(), size: sz, have: ok });
    }
    Status {
        models_dir: dir.display().to_string(),
        total_bytes: total,
        have_bytes: have,
        files,
    }
}

#[derive(Serialize, Clone)]
struct Progress {
    file: String,
    index: usize,
    total_files: usize,
    downloaded: u64,
    file_bytes: u64,
}

pub async fn download_all(app: &AppHandle) -> Result<(), String> {
    let dir = models_dir();
    let client = reqwest::Client::builder()
        .connect_timeout(Duration::from_secs(30))
        .read_timeout(Duration::from_secs(60))
        .build()
        .map_err(|e| e.to_string())?;

    for (i, (src, dst)) in FILES.iter().enumerate() {
        let out = dir.join(dst);
        if present(&out) {
            continue;
        }
        if let Some(parent) = out.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let url = format!("{HF_BASE}/{src}");
        let part = out.with_extension("part");
        let mut last_err = String::new();
        let mut done = false;
        for attempt in 1..=MAX_ATTEMPTS {
            match download_one(app, &client, &url, dst, i, &part).await {
                Ok(()) => {
                    done = true;
                    break;
                }
                Err(e) => {
                    last_err = e;
                    let _ = fs::remove_file(&part);
                    if attempt < MAX_ATTEMPTS {
                        tokio::time::sleep(Duration::from_secs(1 << (attempt - 1))).await;
                    }
                }
            }
        }
        if !done {
            return Err(last_err);
        }
        fs::rename(&part, &out).map_err(|e| e.to_string())?;
    }
    Ok(())
}

async fn download_one(
    app: &AppHandle,
    client: &reqwest::Client,
    url: &str,
    dst: &str,
    index: usize,
    part: &Path,
) -> Result<(), String> {
    let mut resp = client.get(url).send().await.map_err(|e| format!("{dst}: {e}"))?;
    if !resp.status().is_success() {
        return Err(format!("{dst}: HTTP {}", resp.status()));
    }
    let file_bytes = resp.content_length().unwrap_or(0);
    let mut f = fs::File::create(part).map_err(|e| format!("{dst}: {e}"))?;
    let mut downloaded = 0u64;
    let mut last_emit = Instant::now();
    let _ = app.emit(
        "download-progress",
        Progress {
            file: dst.to_string(),
            index,
            total_files: FILES.len(),
            downloaded: 0,
            file_bytes,
        },
    );
    while let Some(chunk) = resp.chunk().await.map_err(|e| format!("{dst}: {e}"))? {
        f.write_all(&chunk).map_err(|e| format!("{dst}: {e}"))?;
        downloaded += chunk.len() as u64;
        if last_emit.elapsed() >= PROGRESS_INTERVAL {
            let _ = app.emit(
                "download-progress",
                Progress {
                    file: dst.to_string(),
                    index,
                    total_files: FILES.len(),
                    downloaded,
                    file_bytes,
                },
            );
            last_emit = Instant::now();
        }
    }
    f.flush().map_err(|e| format!("{dst}: {e}"))?;
    drop(f);
    if file_bytes > 0 && downloaded != file_bytes {
        return Err(format!("{dst}: отримано {downloaded} з {file_bytes} байт"));
    }
    let _ = app.emit(
        "download-progress",
        Progress {
            file: dst.to_string(),
            index,
            total_files: FILES.len(),
            downloaded,
            file_bytes: file_bytes.max(downloaded),
        },
    );
    Ok(())
}
