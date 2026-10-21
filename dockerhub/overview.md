# HolosTTS Core

[![CI](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml)
[![Docker](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/docker.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/docker.yml)
[![GitHub](https://img.shields.io/badge/source-GitHub-black)](https://github.com/Karnemorkon/holos-tts-core)
[![Release](https://img.shields.io/github/v/release/Karnemorkon/holos-tts-core)](https://github.com/Karnemorkon/holos-tts-core/releases/latest)

Production-ready **Ukrainian text-to-speech** service in a single container:
**C++20 + ONNX Runtime**, no Python and no PyTorch at runtime. REST API,
OpenAI-compatible endpoint, WebSocket streaming and a built-in Web UI with **voice cloning**.
Current release: **0.7.1**.

> **Disclaimer:** independent community project, not an official product of the original
> neural network author ([patriotyk](https://github.com/patriotyk)). Model weights are
> MIT-licensed by the author. Provided as is, without warranty.

## Features

- Ukrainian speech synthesis, 24 kHz, **27 preset voices**
- **Voice cloning** from a WAV sample (ONNX voice encoder)
- **INT8 ONNX generator** on CPU — no GPU required, x86-64 and ARM64
- **Automatic verbalization**: Gemma-3-270M int8 (`neural`, default), C++ rules (`rule`), or `off`
- **Stress accentuation**: marisa-trie dictionary (2.9M word forms) + accentor-lite ONNX for OOV
- **Streaming**: OpenAI-style chunked response and WebSocket (`/api/ws/synthesize`)
- **API parity** with [HolosTTS-hybrid](https://github.com/Karnemorkon/HolosTTS-hybrid)
- Multi-arch image: `linux/amd64`, `linux/arm64`; ~290 MB
- ~1.1 GB RAM with neural verbalizer (~470 MB in `rule` mode)

## Quick start

The image **does not bundle models**. Clone the repository to get the compose file and
the model-downloader script, then use the published image instead of building:

```bash
git clone https://github.com/Karnemorkon/holos-tts-core.git
cd holos-tts-core
# in docker-compose.yml replace the `build:` block with:
#   image: morkon06/holostts-core:0.7.1
docker compose up -d
```

Minimal compose (the full version with resource limits and healthcheck is in the repository):

```yaml
services:
  holos-core:
    image: morkon06/holostts-core:0.7.1
    container_name: holos-core
    ports:
      - "8004:8004"
    environment:
      - HOLOS_MODELS_DIR=/app/models
      - HOLOS_VERBALIZER_DIR=/app/models/verbalizer/gemma-int8
      - HOLOS_PORT=8004
      - HOLOS_THREADS=0
      - HOLOS_SPEAKERS_DIR=/app/speakers
    volumes:
      - holos_core_models:/app/models:ro
      - holos_core_speakers:/app/speakers
    depends_on:
      downloader:
        condition: service_completed_successfully
    restart: unless-stopped

  downloader:
    image: python:3.11-slim
    working_dir: /work
    environment:
      - HF_HOME=/work/.hf_cache
    volumes:
      - .:/work
      - holos_core_models:/work/models
    command: >
      sh -c "pip install -q huggingface_hub &&
             python3 scripts/download_models.py --out models"

volumes:
  holos_core_models:
  holos_core_speakers:
```

The `downloader` fetches the public models (~0.6 GB, no token) on first start;
subsequent starts skip existing files. Then open **http://localhost:8004/** and check:

```bash
curl http://localhost:8004/healthz
# {"status":"ok","engine":"holos-tts-core-native","version":"0.7.1","cloning":true,...}
```

## Web UI and voice cloning

Upload a WAV (clean speech), enter a name, and the cloned voice becomes available
everywhere as a normal `voice` (REST, OpenAI API, WebSocket).

```bash
curl -X POST http://localhost:8004/api/voices \
  -H 'Content-Type: application/json' \
  -d '{"name":"My Voice","audio_base64":"<base64 WAV>"}'

curl http://localhost:8004/api/voices
curl -X DELETE http://localhost:8004/api/voices/My%20Voice
```

Cloned voices are stored as `speakers/*.npy` (numpy-compatible with HolosTTS-hybrid).
Do not clone voices without consent.

## API

Custom JSON endpoint:

```bash
curl -X POST http://localhost:8004/api/synthesize \
  -H 'Content-Type: application/json' \
  -d '{"text":"Привіт, це HolosTTS.","voice":"Speaker_43"}'
# -> JSON with duration_sec, verbalized_text and audio_base64 (WAV)
```

OpenAI-compatible endpoint:

```bash
curl -X POST http://localhost:8004/v1/audio/speech \
  -H 'Content-Type: application/json' \
  -d '{"model":"tts-1","input":"Привіт!","voice":"Speaker_43","response_format":"wav"}' \
  -o speech.wav
```

`response_format`: `wav` or `pcm`; `stream: true` enables chunked transfer.
WebSocket: `/api/ws/synthesize` (PCM16 chunks). No authentication.

## Configuration

| Variable | Default | Description |
|---|---|---|
| `HOLOS_PORT` | `8004` | HTTP port |
| `HOLOS_HOST` | `0.0.0.0` | Bind address (portable builds use `127.0.0.1`) |
| `HOLOS_MODELS_DIR` | `/app/models` | Models directory (read-only) |
| `HOLOS_VERBALIZER_DIR` | `""` / image: `onnx-int8` | Neural verbalizer dir; empty = `rule` only. Compose uses `gemma-int8` |
| `HOLOS_SPEAKERS_DIR` | `/app/speakers` | Cloned voices (`speakers/*.npy`) |
| `HOLOS_THREADS` | `0` | ONNX threads, `0` = 80% of available cores (cgroup quota aware) |
| `HOLOS_METRICS` | `0` | Per-request timings in logs/responses |

> The 0.7.1 image's default `HOLOS_VERBALIZER_DIR` points to the legacy M2M100
> directory; if those files are absent the server falls back to `rule`. The compose
> file above sets `gemma-int8`, the recommended mode.

## Image tags

- `latest` — latest release
- `0.7.1` — pinned release
- Published only from `v*` tags; there are no `sha-*` tags.
- Both tags are multi-arch manifests (`linux/amd64` + `linux/arm64`); the ARM64 image
  passes a runtime smoke test (healthz, synthesis, clone) before publication.

## Documentation

- Source and full docs: https://github.com/Karnemorkon/holos-tts-core
- [API](https://github.com/Karnemorkon/holos-tts-core/blob/main/docs/API.md) ·
  [Configuration](https://github.com/Karnemorkon/holos-tts-core/blob/main/docs/CONFIGURATION.md) ·
  [Models](https://github.com/Karnemorkon/holos-tts-core/blob/main/docs/MODELS.md) ·
  [Benchmarks](https://github.com/Karnemorkon/holos-tts-core/blob/main/docs/BENCHMARKS.md) ·
  [Portable builds](https://github.com/Karnemorkon/holos-tts-core/blob/main/docs/PORTABLE.md)
- Releases: https://github.com/Karnemorkon/holos-tts-core/releases
- Original models: https://huggingface.co/patriotyk
- Model mirror used by the image: https://huggingface.co/Morkon/holos-native-models

## License

Project code: see [LICENSE](https://github.com/Karnemorkon/holos-tts-core/blob/main/LICENSE).
Model weights remain under their original MIT license by the model author.
