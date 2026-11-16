use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use serde::Serialize;
use tauri::{AppHandle, Emitter};

const HF_BASE: &str = "https://huggingface.co/Morkon/holos-native-models/resolve/main";
const VOICES_URL: &str =
    "https://huggingface.co/datasets/Morkon/holos-voices-uk-cc0/resolve/main/onnx/voices.npz";
const VOICES_DST: &str = "holos-onnx/voices.npz";
const MAX_ATTEMPTS: u32 = 3;
const PROGRESS_INTERVAL: Duration = Duration::from_millis(100);

// (HF path, relative dest) — дзеркало packaging/linux/download-models.sh.
// Типовий формат — багатоетапний бандл holos-onnx (encoder/prosody/decoder +
// streaming); legacy-граф holos_cpu_int8.onnx більше не завантажується, але
// старі інсталяції з ним продовжують працювати (HOLOS_MODEL_FORMAT=legacy).
// Каталог голосів живе в окремому датасеті й оновлюється завжди (VOICES_URL),
// а не пропускається, як ваги.
const FILES: &[(&str, &str)] = &[
    ("holos-onnx-int8/holos_onnx.json", "holos-onnx/holos_onnx.json"),
    ("holos-onnx-int8/encoder.onnx", "holos-onnx/encoder.onnx"),
    ("holos-onnx-int8/prosody.onnx", "holos-onnx/prosody.onnx"),
    ("holos-onnx-int8/decoder.onnx", "holos-onnx/decoder.onnx"),
    ("holos-onnx-int8/decoder_window.onnx", "holos-onnx/decoder_window.onnx"),
    ("holos-onnx-int8/source.onnx", "holos-onnx/source.onnx"),
    ("stress/stress.trie", "stress.trie"),
    ("accentor/accentor-lite.onnx", "accentor-lite.onnx"),
    ("tts/voice_encoder.onnx", "voice_encoder.onnx"),
    ("verbalizer/gemma-int8/model.onnx", "verbalizer/gemma-int8/model.onnx"),
    ("verbalizer/gemma-int8/tokenizer.model", "verbalizer/gemma-int8/tokenizer.model"),
    ("verbalizer/gemma-int8/config.json", "verbalizer/gemma-int8/config.json"),
    ("verbalizer/gemma-int8/GEMMA-NOTICE.txt", "verbalizer/gemma-int8/GEMMA-NOTICE.txt"),
];

fn all_files() -> impl Iterator<Item = (&'static str, &'static str)> {
    FILES
        .iter()
        .copied()
        .chain(std::iter::once((VOICES_URL, VOICES_DST)))
}

fn url_for(src: &str) -> String {
    if src.starts_with("https://") || src.starts_with("http://") {
        src.to_string()
    } else {
        format!("{HF_BASE}/{src}")
    }
}

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

/// Каталог багатоетапного бандла (`HOLOS_ONNX` для sidecar).
pub fn bundle_dir() -> PathBuf {
    models_dir().join("holos-onnx")
}

/// Маніфест бандла: його наявність = моделі нового формату завантажені.
pub fn bundle_manifest() -> PathBuf {
    bundle_dir().join("holos_onnx.json")
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
    for (_, dst) in all_files() {
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

    // Завантажуємо лише відсутні ваги, але каталог голосів — завжди.
    let jobs: Vec<(&str, &str)> = FILES
        .iter()
        .copied()
        .filter(|(_, dst)| !present(&dir.join(dst)))
        .chain(std::iter::once((VOICES_URL, VOICES_DST)))
        .collect();
    let total_files = jobs.len();

    for (i, &(src, dst)) in jobs.iter().enumerate() {
        let out = dir.join(dst);
        if let Some(parent) = out.parent() {
            fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        let url = url_for(src);
        let part = out.with_extension("part");
        let mut last_err = String::new();
        let mut done = false;
        // Каталог голосів маленький: одна швидка спроба, щоб не гальмувати
        // запуск застосунку без мережі.
        let attempts = if dst == VOICES_DST { 1 } else { MAX_ATTEMPTS };
        for attempt in 1..=attempts {
            match download_one(app, &client, &url, dst, i, total_files, &part).await {
                Ok(()) => {
                    done = true;
                    break;
                }
                Err(e) => {
                    last_err = e;
                    let _ = fs::remove_file(&part);
                    if attempt < attempts {
                        tokio::time::sleep(Duration::from_secs(1 << (attempt - 1))).await;
                    }
                }
            }
        }
        if !done {
            // Не заважаємо запуску офлайн, якщо старий каталог уже є.
            if dst == VOICES_DST && present(&out) {
                eprintln!("voice catalog refresh failed, keeping existing file: {last_err}");
                continue;
            }
            return Err(last_err);
        }
        // Windows rename не перезаписує наявний файл (актуально для каталогу голосів).
        if out.exists() {
            fs::remove_file(&out).map_err(|e| format!("{dst}: {e}"))?;
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
    total_files: usize,
    part: &Path,
) -> Result<(), String> {
    let mut resp = {
        let req = client.get(url);
        let req = if dst == VOICES_DST {
            req.timeout(Duration::from_secs(10))
        } else {
            req
        };
        req.send().await.map_err(|e| format!("{dst}: {e}"))?
    };
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
            total_files,
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
                    total_files,
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
            total_files,
            downloaded,
            file_bytes: file_bytes.max(downloaded),
        },
    );
    Ok(())
}
