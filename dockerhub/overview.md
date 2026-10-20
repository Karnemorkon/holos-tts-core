# HolosTTS Core

[![CI](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml)
[![Docker](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/docker.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/docker.yml)
[![GitHub](https://img.shields.io/badge/source-GitHub-black)](https://github.com/Karnemorkon/holos-tts-core)

Production-ready **Ukrainian text-to-speech** service in a single container:
**C++20 + ONNX Runtime**, no Python and no PyTorch at runtime. REST API, OpenAI-compatible
endpoint, WebSocket streaming and a built-in Web UI with **voice cloning**.

> **Disclaimer:** independent community project, not an official product of the original
> neural network author ([patriotyk](https://github.com/patriotyk)). Model weights are
> MIT-licensed by the author. Provided as is, without warranty.

## Features

- Ukrainian speech synthesis, 24 kHz, **27 preset voices**
- **Voice cloning** from a WAV sample (ONNX voice encoder, 3-10 s of clean speech)
- **INT8 ONNX generator** on CPU — no GPU required, runs on x86-64 and ARM64
- **Automatic verbalization**: Gemma-3-270M int8 (`neural`, default), C++ rules (`rule`), or `off`
- **Stress accentuation**: marisa-trie dictionary (2.9M word forms) + accentor-lite ONNX for OOV
- **Streaming**: OpenAI-style chunked response and WebSocket (`/api/ws/synthesize`)
- **API parity** with [HolosTTS-hybrid](https://github.com/Karnemorkon/HolosTTS-hybrid)
- Multi-arch image: `linux/amd64`, `linux/arm64`
- ~290 MB image; ~1.1 GB RAM with neural verbalizer (~470 MB in `rule` mode)

## Quick start

```yaml
# docker-compose.yml
services:
  holos-core:
    image: morkon06/holostts-core:latest
    container_name: holos-core
    ports:
      - "8004:8004"
    environment:
      - HOLOS_MODELS_DIR=/app/models
      - HOLOS_VERBALIZER_DIR=/app/models/verbalizer/gemma-int8
      - HOLOS_SPEAKERS_DIR=/app/speakers
    volumes:
      - holos_core_models:/app/models:ro
      - holos_core_speakers:/app/speakers
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

The `downloader` service fetches the public models (~0.6 GB, no token) on first start;
subsequent starts skip existing files. Get `scripts/download_models.py` from the
[repository](https://github.com/Karnemorkon/holos-tts-core), or populate the volume manually.

Then open **http://localhost:8004/** and check readiness:

```bash
curl http://localhost:8004/healthz
# {"status":"ok","engine":"holos-tts-core-native","version":"0.6.0","cloning":true,...}
```

## Web UI and voice cloning

The UI provides synthesis and a cloning card: upload a WAV (3-10 s, one speaker, low noise),
enter a name, and the cloned voice becomes available everywhere as a normal `voice`
(REST, OpenAI API, WebSocket).

```bash
# clone
curl -X POST http://localhost:8004/api/voices \
  -H 'Content-Type: application/json' \
  -d '{"name":"My Voice","audio_base64":"<base64 WAV>"}'

# list / delete
curl http://localhost:8004/api/voices
curl -X DELETE http://localhost:8004/api/voices/My%20Voice
```

Cloned voices are stored as `speakers/*.npy` (numpy-compatible with HolosTTS-hybrid).

## API

Custom JSON endpoint:

```bash
curl -X POST http://localhost:8004/api/synthesize \
  -H 'Content-Type: application/json' \
  -d '{"text":"Привіт, це HolosTTS.","voice":"Speaker_0"}'
# -> JSON with duration_sec, verbalized_text and audio_base64 (WAV)
```

OpenAI-compatible endpoint:

```bash
curl -X POST http://localhost:8004/v1/audio/speech \
  -H 'Content-Type: application/json' \
  -d '{"model":"tts-1","input":"Привіт!","voice":"Speaker_0","response_format":"wav"}' \
  -o speech.wav
```

`response_format`: `wav` or `pcm`; `stream: true` enables chunked transfer.
WebSocket: `/api/ws/synthesize` (PCM16 chunks).

## Configuration

| Variable | Default | Description |
|---|---|---|
| `HOLOS_PORT` | `8004` | HTTP port |
| `HOLOS_MODELS_DIR` | `/app/models` | Models directory (read-only) |
| `HOLOS_VERBALIZER_DIR` | gemma-int8 | `neural` verbalizer dir; empty = `rule` only |
| `HOLOS_SPEAKERS_DIR` | `/app/speakers` | Cloned voices (`speakers/*.npy`) |
| `HOLOS_THREADS` | auto | ONNX threads, `0` = 80% of CPU cores |
| `HOLOS_METRICS` | `0` | Per-request timings in logs/responses |
| `HOLOS_VERBALIZER` | `neural` | `neural` / `rule` / `off` |

## Image tags

- `latest` — latest release
- `0.6.0` — pinned release
- `sha-<commit>` — immutable build

## Links

- Source, full documentation and benchmarks: https://github.com/Karnemorkon/holos-tts-core
- Releases: https://github.com/Karnemorkon/holos-tts-core/releases
- Original models: https://huggingface.co/patriotyk
- Model mirror used by the image: https://huggingface.co/Morkon/holos-native-models

Do not clone voices without consent. Use responsibly.

## License

Project code: see [LICENSE](https://github.com/Karnemorkon/holos-tts-core/blob/main/LICENSE).
Model weights remain under their original MIT license by the model author.
