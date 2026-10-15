# holos-tts-core

[![CI](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml)

C++ native core для **HolosTTS** (український text-to-speech). Етап 3 ТЗ:
заміна Python-сервісу легшим C++ ядром з API-паритетом.

> **Статус: ядро готове до роботи (M6).** Повний пайплайн: препроцес →
> нейро-вербалізація (M2M100 ONNX int8) → стрес (словник marisa-trie +
> нейро-фолбек accentor) → ipa_uk фонемізація → токенізація → ORT-генерація →
> PCM/WAV. Drogon HTTP/WS сервер на `:8004` з API-паритетом hybrid та
> OpenAI `/v1/audio/speech`. Тривалість аудіо збігається з
> `HolosTTS-hybrid` до **≤0.02 с**; neural-вербалізація — **дослівний паритет**.

## Цілі (ТЗ Етап 3)

- RAM ≤ 400 МБ (проти ~1.5 ГБ у Python) — rule-only конфіг: 468 МБ
- Cold start < 0.3 с, **Time-To-First-Byte ≤ 200 мс**
- API-паритет із `Karnemorkon/HolosTTS-hybrid` (Wyoming-проксі без змін)
- Автономна збірка без Python (Linux; Windows/Android NDK — надалі)

## Архітектура пайплайну

```
текст → препроцес → вербалізація (M2M100 ONNX int8 або rule)
      → стрес (словник marisa-trie → neural accentor-фолбек для OOV)
      → фонемізація (ipa_uk) → токенізація → генерація (ONNX/ORT) → PCM/WAV
```

**Стрес — словниковий + neural фолбек.** `stress.trie` (marisa-trie, 2.9М
ключів) покриває більшість форм за мікросекунди. Слова поза словником
(або неоднозначні, варіант A: skip) обробляє **accentor-lite** — LSTM
експортований в ONNX (single-file, ~1.2 МБ), той самий `ukrainian_accentor`,
що й у Python-конвеєрі. Unicode-регістр слова нормалізується utf8proc
(паритет з Python `word.lower()/title()`).

**Вербалізація — два режими** (`auto_verbalize`):

| Режим | Реалізація | Латентність | Якість |
|---|---|---|---|
| `neural` (типово) | M2M100-630M-distilled, ONNX int8, KV-cache | ~3–4 с на речення (CPU) | дослівний паритет з hybrid (4/4 MATCH) |
| `rule` | C++ регулярки (цифри/дати/час) | мілісекунди | базовий (граматика відмінювання гірша за neural) |
| `off` / `false` | текст як є | 0 | — |

Legacy bool `auto_verbalize: true` → `neural`, `false` → `off`.

## Моделі

Усі моделі у HF-репозиторії **[Morkon/holos-native-models](https://huggingface.co/Morkon/holos-native-models)**
(публічні, ~2.1 ГБ):

| Компонент | Шлях у репо | Застосунок |
|---|---|---|
| TTS ONNX int8 + vocab + voices | `tts/` | генерація аудіо |
| Neural verbalizer (M2M100 ONNX int8) | `verbalizer/onnx-int8/` | цифри→слова (якість) |
| Neural verbalizer (CT2 int8) | `verbalizer/ct2-int8/` | той самий, CTranslate2 |
| Accentor (ONNX, single-file) | `accentor/` | нейро-фолбек наголосу |
| Stress trie | `stress/stress.trie` | словниковий наголос |

Завантаження одним скриптом (розкладка під `HOLOS_MODELS_DIR`):

```bash
pip install huggingface_hub
python3 scripts/download_models.py --out models
```

## Швидкий старт

Кроки від нуля до працюючого сервера (потрібен Docker; ~2.1 ГБ на моделі).

**1. Запустити** — один із варіантів:

```bash
# варіант A: з джерела — моделі завантажуються АВТОМАТИЧНО
docker compose up -d --build

# варіант B: образ з Docker Hub — моделі завантажити вручну
pip install huggingface_hub && python3 scripts/download_models.py --out models
docker run -d -p 8004:8004 -v $PWD/models:/app/models:ro \
    --name holos-core morkon06/holostts-core:latest
```

Перший `docker compose up -d` сам завантажує моделі (~2.1 ГБ, публічний HF,
без токена) у named volume `holos_core_models` і запускає сервер одразу після
завершення; подальші старти — миттєві (skipped, якщо моделі вже на місці).
Примусове перезавантаження:

```bash
docker compose run --rm downloader \
    python3 scripts/download_models.py --out models --force
```

**2. Перевірити:**

```bash
curl localhost:8004/healthz
# -> {"engine":"holos-tts-core-native","status":"ok","verbalizer":"neural+rule",...}

# Web-UI: відкрити http://localhost:8004/ у браузері
```

**3. Перший синтез** (WAV-файл `out.wav`):

```bash
curl -s -X POST localhost:8004/api/synthesize \
    -H "Content-Type: application/json" \
    -d '{"text":"Привіт, світе! Це тест синтезу мовлення українською.","voice":"Speaker_43"}' \
    | python3 -c "import sys,json,base64; open('out.wav','wb').write(base64.b64decode(json.load(sys.stdin)['audio_base64']))"
```

**Нотатки:**

- Нейро-дефолт тримає ~1.7 ГБ RSS у простої (+70 МБ під час генерації) —
  давайте контейнеру ліміт ≥2 ГБ. Rule-only (без neural-вербалізатора):
  `-e HOLOS_VERBALIZER_DIR=""` → ~468 МБ.
- Голос за замовчуванням core-API: `Speaker_0` (hybrid використовує
  `Speaker_84`); повний список — `GET /api/voices`.
- Запуск нативного бінарника (без Docker) — див. розділ «Запуск сервера» нижче.

## Залежності

| Залежність | Призначення | Спосіб |
|---|---|---|
| ONNX Runtime 1.31 | генерація аудіо + neural verbalizer + accentor | prebuilt tarball |
| sentencepiece | токенізатор M2M100 | apt (`libsentencepiece-dev`) |
| marisa-trie | словник наголосів | FetchContent |
| utf8proc, pcre2, nlohmann_json, dr_wav | Unicode/regex/JSON/WAV | FetchContent |
| Drogon | HTTP/WS сервер | FetchContent |
| CTranslate2 | (опційно) CT2-вербалізація | FetchContent |

## Збірка

```bash
# повна збірка (ORT + сервер + neural verbalizer)
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHOLOS_WITH_ORT=ON \
    -DHOLOS_WITH_SERVER=ON \
    -DHOLOS_WITH_VERBALIZER=ON \
    -DHOLOS_ORT_ROOT=/opt/ort-1.31.0
cmake --build build --parallel

# golden-тест neural verbalizer (30/30 vs Python ONNX)
cmake -S . -B build -DHOLOS_TEST_VERBALIZER_DIR=/path/to/verbalizer/onnx-int8
ctest --test-dir build -R verbalizer_neural
```

Золотий тест пропускається (exit 77), якщо `HOLOS_TEST_VERBALIZER_DIR` не задано.
CI (ubuntu-24.04) збирає конфіг `ORT=ON, SERVER=OFF` і проганяє golden-тести
без моделей.

## Запуск сервера

```bash
export HOLOS_MODELS_DIR=$PWD/models
export HOLOS_VERBALIZER_DIR=$PWD/models/verbalizer/onnx-int8   # або "" для rule-only
./build/server/holos-server   # :8004
```

| Env | Дефолт | Призначення |
|---|---|---|
| `HOLOS_MODELS_DIR` | `/app/models` | база моделей |
| `HOLOS_TRIE` | `$MODELS/stress.trie` | словник наголосів |
| `HOLOS_VOCAB` | `$MODELS/vocab.json` | токенізатор |
| `HOLOS_VOICES` | `$MODELS/voices.json` | пресети голосів |
| `HOLOS_ONNX` | `$MODELS/holos_cpu_int8.onnx` | TTS-модель |
| `HOLOS_ACCENTOR` | `$MODELS/accentor-lite.onnx` | нейро-фолбек наголосу |
| `HOLOS_VERBALIZER_DIR` | `""` (rule-only) | neural verbalizer; порожній = rule |
| `HOLOS_PORT` | `8004` | HTTP-порт |

### API (паритет HolosTTS-hybrid)

- `GET /healthz` — статус; містить `verbalizer: "neural+rule"|"rule"`
- `GET /api/voices` — список голосів
- `POST /api/synthesize` — `{text, voice?, speed?, raw_stress?, auto_verbalize?, format?}`;
  відповідь містить `text` (оригінал), `verbalized_text`, `audio_base64` (WAV)
- `POST /api/verbalize` — `{text, mode?: "neural"|"rule"}`; вербалізація без синтезу
- `POST /v1/audio/speech` — OpenAI-сумісний ендпоінт (streaming підтримується)
- `WS /api/ws/synthesize` — чанковий стрім PCM16
- `GET /` — UI (dropdown автовербалізації: neural/rule/off)

`auto_verbalize`: `"neural"` (типово) | `"rule"` | `"off" | false | true` (legacy).

### Паритет з hybrid (виміряно, voice=Speaker_43)

| Текст | hybrid | core | Δ |
|---|---|---|---|
| «Привіт, світе! Це тест синтезу мовлення українською.» | 3.60 с | 3.62 с | +0.02 |
| «Купив 25 машин 12 березня 2025 року о 14:30…» | 8.07 с | 8.07 с | 0.00 |
| «Загальна вартість становить 12 450 гривень…» | 7.55 с | 7.55 с | 0.00 |
| «Науково-дослідний інститут працює над проєктом уже три роки.» | 4.22 с | 4.22 с | 0.00 |

Neural-вербалізація: 4/4 дослівних збігів з hybrid. Відомі відмінності:
`/api/voices` віддає відсортований список (hybrid — порядок `voices.pt`);
rule-вербалізатор гірше відмінює («дванадцять березня» замість «дванадцятого»).

### Бенчмарк (Xeon, 9 потоків, warm)

| Конфіг | RSS | TTFB `/api/synthesize` (теплий) | neural verbalize |
|---|---|---|---|
| neural (default) | **1.65 ГБ** | ~0.5–1.4 с | ~3–4 с/речення |
| rule-only (`HOLOS_VERBALIZER_DIR=""`) | **468 МБ** | ~0.5–0.8 с | — (мс) |

M2M100-630M int8 додає ~1.2 ГБ RSS — свідомий trade-off «якість за
замовчуванням». Для RAM-constrained деплоїв запускайте з порожнім
`HOLOS_VERBALIZER_DIR` (вербалізація rule, UI дозволяє перемикати режими
per-request).

### Docker

Опублікований образ: **[`morkon06/holostts-core`](https://hub.docker.com/r/morkon06/holostts-core)**
(companion: `morkon06/holostts-hybrid`).

```bash
# з локального джерела: моделі автовантаження у named volume holos_core_models
docker compose up -d --build

# з Docker Hub: див. «Швидкий старт», варіант B (моделі вручну у ./models)
```

Моделі живуть у named volume `holos_core_models` (монтуються на `/app/models`);
сервіс `downloader` у compose завантажує їх перед стартом сервера (ідемпотентно).
Багато-архітектурна збірка (amd64+arm64) — надалі.

## Структура

```
src/core/      бібліотека ядра (текст, вербалізація, стрес, фонеми, ONNX, аудіо)
src/cli/       holos-cli
server/        Drogon HTTP/WS (:8004)
scripts/       download_models.py (HF → models/)
tools/         допоміжні скрипти (конвертація int8, паритет PCM)
tests/         unit + golden-тести
cmake/         FetchContent-залежності
docker/        multi-stage Dockerfile
```

## Дорожня карта

- **M0** ✅ каркас: CMake, словниковий стрес-модуль, Dockerfile
- **M1** ✅ ядро: препроцес, ipa_uk, токенізатор, ORT-синтез, CLI
- **M2** ✅ сервер: Drogon HTTP/WS, стрім, OpenAI-ендпоінт, UI
- **M3** ✅ Docker: multi-stage образ, compose, HF download script, Docker Hub
- **M4** ✅ neural verbalizer: M2M100 ONNX int8 (KV-cache, greedy), golden 30/30
- **M5** ✅ API-інтеграція: `auto_verbalize: neural|rule|off`, default neural
- **M6** ✅ паритет стресу: accentor single-file ONNX (відновлено втрачений
  external-data експорт), Unicode lower/title, UTF-8-safe вставка U+0301;
  тривалість аудіо Δ ≤ 0.02 с vs hybrid

Надалі: граматика rule-вербалізатора, порядок `/api/voices`, CT2-вербалізація,
мульті-архітектурні образи.

## Ліцензія

MIT. Моделі та `stress.trie` належать їх авторам (див. `patriotyk/*`,
`ukrainian-word-stress`, `ukrainian_accentor`, M2M100 Helsinki-NLP).
