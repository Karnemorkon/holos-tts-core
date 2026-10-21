# Конфігурація

## Змінні середовища

| Змінна | Типово | Що робить |
|---|---|---|
| `HOLOS_MODELS_DIR` | `/app/models` | база моделей |
| `HOLOS_TRIE` | `$MODELS/stress.trie` | словник наголосів |
| `HOLOS_VOCAB` | `$MODELS/vocab.json` | токенізатор |
| `HOLOS_VOICES` | `$MODELS/voices.json` | пресети голосів |
| `HOLOS_ONNX` | `$MODELS/holos_cpu_int8.onnx` | TTS-модель |
| `HOLOS_ACCENTOR` | `$MODELS/accentor-lite.onnx` | нейро-фолбек наголосу |
| `HOLOS_VOICE_ENCODER` | `$MODELS/voice_encoder.onnx` | енкодер для клонування (немає файлу → клонування вимкнено) |
| `HOLOS_SPEAKERS_DIR` | `/app/speakers` | каталог клонованих голосів (`*.npy`) |
| `HOLOS_VERBALIZER_DIR` | `""` (rule-only) | neural verbalizer; порожній = rule |
| `HOLOS_PORT` | `8004` | HTTP-порт |
| `HOLOS_HOST` | `0.0.0.0` (сервер); `127.0.0.1` у портативних | інтерфейс; `0.0.0.0` відкриває для LAN |
| `HOLOS_THREADS` | авто | потоки ONNX Runtime (0 = авто: 80% доступних ядер, з урахуванням cgroup-квоти) |
| `HOLOS_METRICS` | `0` | `1` = `[metrics]` логи + `timings` у відповідях (для бенчів) |

Примітки:

- У **Docker-образі 0.7.1** `HOLOS_VERBALIZER_DIR` типово вказує на legacy
  `verbalizer/onnx-int8` (M2M100). Якщо цих файлів немає, сервер пише warning і
  переходить у `rule`-режим. `docker-compose.yml` перевизначає шлях на
  `verbalizer/gemma-int8` — рекомендований варіант.
- У **нативному бінарнику** без `HOLOS_VERBALIZER_DIR` типово вмикається rule-only.
- `HOLOS_THREADS=0` враховує cgroup-квоту контейнера; для 16-ядерного хоста це ~12–13
  потоків (80%). Для малих VPS задайте значення явно.
- `HOLOS_METRICS=1` додає таймінги до відповідей; не вмикайте на постійній основі.

## Теги образів

| Тег | Архітектури | Для чого |
|---|---|---|
| `latest` | linux/amd64 + linux/arm64 | останній реліз; оновлюється лише з тегів `v*` |
| `vX.Y.Z` (напр. `0.7.1`) | linux/amd64 + linux/arm64 | пін релізу |

`sha-*`-тегів немає: Docker Hub публікується лише з тегів `v*`. ARM64 збирається нативно
(`ubuntu-24.04-arm`, без QEMU) і проходить runtime-смоук (healthz + синтез + клон)
перед публікацією маніфесту.

## Docker Hub

Образ [`morkon06/holostts-core`](https://hub.docker.com/r/morkon06/holostts-core)
**не містить моделей** — їх треба змонтувати. Найпростіше взяти compose з репозиторію
й замінити збірку на готовий образ (downloader лишається):

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
      - HOLOS_THREADS=${HOLOS_THREADS:-0}
      - HOLOS_SPEAKERS_DIR=/app/speakers
    volumes:
      - holos_core_models:/app/models:ro
      - holos_core_speakers:/app/speakers
    depends_on:
      downloader:
        condition: service_completed_successfully
    deploy:
      resources:
        limits:
          cpus: "${HOLOS_CPUS:-12.8}"
          memory: 3g
    memswap_limit: 3g
    logging:
      driver: json-file
      options:
        max-size: "10m"
        max-file: "3"
    healthcheck:
      test: ["CMD", "curl", "-sf", "http://localhost:8004/healthz"]
      interval: 30s
      timeout: 10s
      retries: 5
      start_period: 120s
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
    name: holos_core_models
  holos_core_speakers:
    name: holos_core_speakers
```

`downloader` запускається щоразу, але завантажує лише відсутні файли. Примусово:

```bash
docker compose run --rm downloader \
    python3 scripts/download_models.py --out models --force
```

Моделі можна підготувати заздалегідь і змонтувати каталог замість named volume:

```bash
pip install huggingface_hub
python3 scripts/download_models.py --out models
# у compose: -v $PWD/models:/app/models:ro, прибрати downloader і depends_on
```

Ліміти `cpus: "${HOLOS_CPUS:-12.8}"` (80% 16-ядерного хоста) і `memory: 3g` правте під
свій хост. Для rule-only задайте `HOLOS_VERBALIZER_DIR=""` (економія ~0.6 ГБ RSS).

## Порти та томи

- `8004/tcp` — HTTP API + Web UI.
- `holos_core_models` — моделі, read-only для сервера.
- `holos_core_speakers` — клоновані голоси (`speakers/*.npy`), writable.

## Безпека

Автентифікації немає. Не відкривайте `8004` у публічну мережу напряму: використовуйте
reverse proxy з TLS/basic auth або прив’яжіть порт до `127.0.0.1`
(`- "127.0.0.1:8004:8004"`). Для LAN-доступу в портативних збірках —
`HOLOS_HOST=0.0.0.0` (теж без автентифікації).
