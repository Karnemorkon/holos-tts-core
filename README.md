# holos-tts-core

C++ native core for **HolosTTS** (Ukrainian text-to-speech). Етап 3 ТЗ: заміна
Python-сервісу легшим C++ ядром з API-паритетом.

> **Статус: M4+ (ядро готове).** Реалізовано повний пайплайн:
> препроцес → **нейро-вербалізація (M2M100 ONNX int8)** → словниковий стрес
> → ipa_uk фонемізація → токенізація → ORT-генерація → PCM/WAV, плюс
> Drogon HTTP/WS сервер на `:8004` з паритетом API і OpenAI `/v1/audio/speech`.

## Цілі (ТЗ Етап 3)

- RAM ≤ 400 МБ (проти ~1.5 ГБ у Python)
- Cold start < 0.3 с, **Time-To-First-Byte ≤ 200 мс**
- API-паритет із `Karnemorkon/HolosTTS-hybrid` (Wyoming-проксі без змін)
- Автономна збірка без Python (Linux; Windows/Android NDK — надалі)

## Архітектура пайплайну

```
текст → препроцес → вербалізація(M2M100 ONNX int8 або rule) → стрес(словник marisa-trie)
      → фонемізація(ipa_uk) → токенізація → генерація(ONNX/ORT) → PCM/WAV
```

**Стрес — словниковий, без нейро (варіант A).** `stress.trie` (marisa-trie)
дає 98.7% словникових форм з одним наголосом за мікросекунди. ByT5 на CPU мав
6–17 с/речення і поступається точністю — відхилено.

**Вербалізація — два режими** (`auto_verbalize`):

| Режим | Реалізація | Латентність | Якість (EXACT vs Python) |
|---|---|---|---|
| `neural` (типово) | M2M100-630M-distilled, ONNX int8, KV-cache | ~4 с на речення (CPU) | 87% EXACT / 93% NORMALIZED |
| `rule` | C++ регулярки (цифри/дати/час) | мілісекунди | базовий |
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
| Accentor (ONNX) | `accentor/` | нейро-фолбек наголосу |
| Stress trie | `stress/stress.trie` | словниковий наголос |

Завантаження одним скриптом (розкладка під `HOLOS_MODELS_DIR`):

```bash
pip install huggingface_hub
python3 scripts/download_models.py --out models
```

## Залежності

| Залежність | Призначення | Спосіб |
|---|---|---|
| ONNX Runtime 1.31 | генерація аудіо + neural verbalizer | prebuilt tarball |
| CTranslate2 | (опційно) CT2-вербалізація | FetchContent |
| sentencepiece | токенізатор M2M100 | FetchContent |
| marisa-trie | словник наголосів | FetchContent |
| Drogon | HTTP/WS сервер | FetchContent |
| nlohmann_json, dr_wav | утиліти | FetchContent |

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
- `WS /api/synthesize/stream` — чанковий стрім PCM16
- `GET /` — UI (dropdown автовербалізації: neural/rule/off)

`auto_verbalize`: `"neural"` (типово) | `"rule"` | `"off" | false | true` (legacy).

### Бенчмарк (Xeon, 9 потоків, warm)

| Конфіг | RSS | TTFB `/api/synthesize` (теплий) | neural verbalize |
|---|---|---|---|
| neural (default) | **1.65 ГБ** | ~0.52–0.95 с | ~3.4–4.1 с/речення |
| rule-only (`HOLOS_VERBALIZER_DIR=""`) | **468 МБ** | ~0.51–0.84 с | — (мс) |

M2M100-630M int8 додає ~1.2 ГБ RSS — свідомий trade-off «якість за замовчуванням».
Для RAM-constrained деплоїв запускайте з порожнім `HOLOS_VERBALIZER_DIR`
(вербалізація rule, UI дозволяє перемикати режими per-request).

### Docker

```bash
docker build -f docker/Dockerfile -t holos-tts-core:local .
```

Моделі монтуються volume'ом на `/app/models`. Багато-архітектурна збірка
(amd64+arm64 → Docker Hub `morkon06/holostts-core`) — M3.

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
- **M3** (частково) Docker; CI/бенчі/RAM — у Фазі 6+
- **M4** ✅ neural verbalizer: M2M100 ONNX int8 (KV-cache, greedy), golden 30/30
- **M5** ✅ API-інтеграція: `auto_verbalize: neural|rule|off`, default neural

## Ліцензія

MIT. Моделі та `stress.trie` належать їх авторам (див. `patriotyk/*`,
`ukrainian-word-stress`, M2M100 Helsinki-NLP).
