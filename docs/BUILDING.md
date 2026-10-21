# Збірка з джерела

## Залежності

ONNX Runtime 1.31 (prebuilt tarball), sentencepiece (apt), Drogon, marisa-trie,
utf8proc, PCRE2, nlohmann_json, dr_wav (FetchContent).

## CMake

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHOLOS_WITH_ORT=ON \
    -DHOLOS_WITH_SERVER=ON \
    -DHOLOS_WITH_VERBALIZER=ON \
    -DHOLOS_ORT_ROOT=/opt/ort-1.31.0
cmake --build build --parallel
```

## Golden-тести

Пропускаються, якщо шлях не задано.

```bash
# neural verbalizer (neural 30/30, gemma 46/46 vs Python ONNX)
cmake -S . -B build \
    -DHOLOS_TEST_VERBALIZER_DIR=/path/to/verbalizer/onnx-int8 \
    -DHOLOS_TEST_VERBALIZER_GEMMA_DIR=/path/to/verbalizer/gemma-int8
ctest --test-dir build -R "verbalizer_neural|gemma_prep|verbalizer_gemma"

# voice encoder (клонування; cos ≥0.995 vs Python ORT)
cmake -S . -B build -DHOLOS_TEST_VOICE_ENCODER=/path/to/voice_encoder.onnx
ctest --test-dir build -R "voice_encoder|audio_io|custom_voices"
```

Повний ctest — 81 тест (єдина відома невдача — upstream Drogon `CookieSameSite`,
не повʼязана з проєктом).

CI збирає конфіг `ORT=ON, SERVER=OFF`, проганяє тести без моделей. `docker.yml`
збирає amd64+arm64, проганяє arm64-смоук і публікує маніфест лише з тегів `v*`
(`latest` + версійний тег; `sha-*` не створюються).

## Нативний запуск

```bash
export HOLOS_MODELS_DIR=$PWD/models
export HOLOS_VERBALIZER_DIR=$PWD/models/verbalizer/gemma-int8   # або "" для rule-only
./build/server/holos-server   # :8004
```

## Docker

```bash
docker build -f docker/Dockerfile -t holos-tts-core:local .
# або
docker compose build holos-core
```

Образ не містить моделей — вони монтуються в `/app/models` (див.
[CONFIGURATION.md](CONFIGURATION.md#docker-hub)).

Структура репозиторію — [ARCHITECTURE.md](ARCHITECTURE.md#структура-репозиторію).
