# holos-tts-core

[![Stand With Ukraine](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/banner-direct-single.svg)](https://stand-with-ukraine.pp.ua)
[![Made in Ukraine](https://img.shields.io/badge/made_in-Ukraine-ffd700.svg?labelColor=0057b7)](https://stand-with-ukraine.pp.ua)
[![CI](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml)
[![Docker](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/docker.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/docker.yml)
[![Portable](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/portable.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/portable.yml)
[![Docker Hub](https://img.shields.io/badge/docker-morkon06%2Fholostts--core-blue)](https://hub.docker.com/r/morkon06/holostts-core)

---

> ⚠️ **Відмова від відповідальності / Disclaimer**
>
> * **UA:** Цей проєкт є незалежною аматорською розробкою (нативною C++ обгорткою) і **не є офіційним продуктом** автора оригінальної нейромережі. Він жодним чином не афілійований, не спонсорується і не підтримується розробником [patriotyk](https://github.com/patriotyk). Усі права на оригінальні моделі, ваги та препроцес належать їхньому законному автору. Проєкт надається «як є» (as is), автор цієї обгортки не несе відповідальності за можливі збої чи використання сервісу.
> * **EN:** This project is an independent, community-driven native C++ wrapper and **is not an official product** of the original neural network creator. It is not affiliated with, endorsed, or sponsored by [patriotyk](https://github.com/patriotyk). All rights to the original models and weights belong to their respective owner. The software is provided «as is», without warranty of any kind.

> **English summary:** production-ready Dockerized **Ukrainian text-to-speech** service
> (REST API + Web UI, port 8004) written in **C++20** — no Python and no PyTorch at runtime.
> It runs the [HolosTTS](https://github.com/patriotyk/HolosTTS) neural network with an
> **INT8 ONNX generator**, a **Gemma-3-270M verbalizer**, dictionary + neural stress and
> **voice cloning** (ONNX voice encoder) on CPU via ONNX Runtime. Drogon HTTP/WS server with
> **API parity** to [HolosTTS-hybrid](https://github.com/Karnemorkon/HolosTTS-hybrid)
> (audio duration within ≤0.02 s, neural verbalization verbatim-equal).
> Model weights are MIT-licensed by [patriotyk](https://huggingface.co/patriotyk).

Нативне C++ ядро українського синтезу мовлення: один бінарник + ONNX Runtime, жодного
Python у рантаймі. Перший запуск сам довантажує моделі (~0.6 ГБ) у named volume.

## Можливості

- **Синтез українською** 24 кГц: 27 пресетів голосів + **клонування голосу з WAV**
  (у Web UI клон зберігається під власним імʼям і стає звичайним `voice` для REST/OpenAI API)
- **Нативний C++20**: Drogon HTTP/WS + ONNX Runtime; без Python, PyTorch і Gradio у рантаймі
- **INT8 ONNX-генератор** (`holos_cpu_int8.onnx`) — швидше за fp32 на CPU
- **Автовербалізація**: `neural` (Gemma-3-270M int8, типово) / `rule` (C++ регулярки) / `off`
- **Наголошування**: словник marisa-trie (2.9М форм) + accentor-lite ONNX для слів поза словником
- **Потоковий синтез**: REST-стрім (`/v1/audio/speech`) і WebSocket `/api/ws/synthesize` (PCM16-чанки)
- **API-паритет з HolosTTS-hybrid**: тривалість аудіо Δ ≤0.02 с; та сама схема запитів/відповідей
- **Docker**: multi-stage образ ~290 МБ, named volumes, healthcheck, ліміти CPU/RAM, ротація логів
- **Портативні збірки без Docker**: Linux x64 (AppImage із вбудованими моделями, tar.gz)
  і Windows x64 (zip); tar.gz/zip довантажують моделі при першому запуску
- **Тести**: 81 unit/golden-тест; golden-гейти verbalizer 46/46 і voice encoder cos 0.99998

## Швидкий старт

```bash
git clone https://github.com/Karnemorkon/holos-tts-core.git
cd holos-tts-core
docker compose up -d --build
# перший запуск: довантаження моделей (~0.6 ГБ) у volume, потім:
curl http://localhost:8004/healthz
# Web UI: http://localhost:8004/
# логи першого запуску: docker compose logs -f holos-core
```

Ознака готовності:
`{"status":"ok","engine":"holos-tts-core-native","version":"0.6.1","verbalizer":"neural+rule","cloning":true,...}`.

Потрібен Docker + Compose і ≥2 ГБ RAM (neural-дефолт тримає ~1.1 ГБ RSS; rule-only — ~468 МБ).
Сервіс `downloader` у compose завантажує лише відсутні файли, тож оновлення образу не тягне
моделі заново. Примусове перезавантаження:

```bash
docker compose run --rm downloader \
    python3 scripts/download_models.py --out models --force
```

Перший синтез (WAV `out.wav`):

```bash
curl -s -X POST http://localhost:8004/api/synthesize \
  -H 'Content-Type: application/json' \
  -d '{"text":"Привіт, світе! Це тест синтезу мовлення українською.","voice":"Speaker_43"}' \
  | python3 -c 'import sys,json,base64; open("out.wav","wb").write(base64.b64decode(json.load(sys.stdin)["audio_base64"]))'
```

## Розгортання через Docker Hub

Образ: [`morkon06/holostts-core`](https://hub.docker.com/r/morkon06/holostts-core)
(оновлюється автоматично з `main` через GitHub Actions; теги `latest`, `sha-*`, версійні `v*`).

```bash
docker pull morkon06/holostts-core:latest
```

Образ **не містить моделей** — їх треба змонтувати. Найпростіше — compose з репозиторію,
замінивши збірку на готовий образ (downloader лишається):

```yaml
services:
  holos-core:
    image: morkon06/holostts-core:latest
    # build: … — прибрати або закоментувати
    container_name: holos-core
    ports:
      - "8004:8004"
    environment:
      - HOLOS_MODELS_DIR=/app/models
      - HOLOS_VERBALIZER_DIR=/app/models/verbalizer/gemma-int8
      - HOLOS_PORT=8004
      - HOLOS_SPEAKERS_DIR=/app/speakers
    volumes:
      - holos_core_models:/app/models:ro
      - holos_core_speakers:/app/speakers
    depends_on:
      downloader:
        condition: service_completed_successfully
    restart: unless-stopped
```

Моделі також можна підготувати заздалегідь (`pip install huggingface_hub &&
python3 scripts/download_models.py --out models`) і змонтувати каталог
`-v $PWD/models:/app/models:ro` замість named volume.

Теги: `latest` — поточна `main`, multi-arch **linux/amd64 + linux/arm64**;
`sha-<короткий>` — конкретний коміт; `vX.Y.Z` (наприклад `0.6.1`) — пін релізу.
arm64-образ збирається нативно та проходить runtime-смоук (healthz + синтез + клон)
у CI перед публікацією маніфесту.


## Портативна збірка (без Docker)

Готові файли — у [Releases](https://github.com/Karnemorkon/holos-tts-core/releases):
`holos-tts-core-<версія>-x86_64.AppImage`,
`holos-tts-core-<версія>-linux-x64.tar.gz` і `holos-tts-core-<версія>-windows-x64.zip`.
AppImage — самодостатній (~0.6 ГБ): моделі всередині, працює офлайн.
tar.gz/zip — легкі (~17/12 МБ): моделі довантажує скрипт при першому запуску.

**Linux x64 AppImage** (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch; glibc ≥ 2.35):

```bash
chmod +x holos-tts-core-0.6.1-x86_64.AppImage
./holos-tts-core-0.6.1-x86_64.AppImage   # UI: http://127.0.0.1:8004/
```

- Подвійний клік (з Dolphin/менеджера файлів) відкриває **вікно терміналу** з логами;
  потрібен один із: `konsole`, `gnome-terminal`, `xfce4-terminal`, `mate-terminal`,
  `x-terminal-emulator`, `kitty`, `alacritty`, `foot`, `xterm`. Фонового «тихого»
  запуску немає — без терміналу AppImage завершиться з підказкою.
- Зупинка: `Ctrl+C` у вікні терміналу або `./holos-tts-core-0.6.1-x86_64.AppImage --stop`
  (працює і для запущеного подвійним кліком; pid-файл + SIGTERM/SIGKILL).
- Другий екземпляр одразу завершується з повідомленням «порт зайнятий».
- Моделі вбудовані; перший запуск не потребує мережі.
- Браузер відкривається автоматично після готовності сервера
  (`HOLOS_OPEN_BROWSER=0` — без браузера); якщо не відкрився — див.
  `~/.local/share/holos-tts-core/browser.log`.
- Потрібен FUSE2: Debian/Ubuntu — `libfuse2` або `libfuse2t64`, Arch — `fuse2`,
  Fedora — `fuse-libs`. Без FUSE: `./holos-tts-core-0.6.1-x86_64.AppImage --appimage-extract-and-run`.
- Дані, клоновані голоси й логи: `${XDG_DATA_HOME:-~/.local/share}/holos-tts-core`;
  `HOLOS_SPEAKERS_DIR` змінює каталог голосів.
- `HOLOS_MODELS_DIR`, `HOLOS_VERBALIZER_DIR`, `HOLOS_VOICE_ENCODER` уже вказують
  на вбудовані моделі; `HOLOS_VERBALIZER_DIR=""` вмикає rule-only.
- `--cli`, `--version`, `--help`, `--stop` — CLI, довідка та зупинка.

**Linux x64 tar.gz** (glibc ≥ 2.35, тобто Ubuntu 22.04+; решта бібліотек у комплекті):

```bash
tar xzf holos-tts-core-0.6.1-linux-x64.tar.gz
cd holos-tts-core
./download-models.sh
./holos-tts.sh          # UI: http://127.0.0.1:8004/
```

**Windows x64** (Windows 10/11 + Visual C++ Redistributable 2015-2022):

```powershell
Expand-Archive holos-tts-core-0.6.1-windows-x64.zip -DestinationPath .
cd holos-tts-core
powershell -ExecutionPolicy Bypass -File download-models.ps1
holos-tts.bat
```

Типово сервіс слухає `127.0.0.1:8004`; `HOLOS_PORT` змінює порт, `HOLOS_HOST=0.0.0.0`
відкриває для LAN (без автентифікації). У tar.gz/zip моделі та клоновані голоси
лежать у `models/` і `speakers/` поруч із лаунчером, тож каталог можна переносити
цілком; в AppImage моделі всередині образу, а голоси та логи — у
`~/.local/share/holos-tts-core`. Типово використовується neural verbalizer і 80%
ядер CPU для ONNX Runtime.

## Клонування голосу

UI: картка **«Клонування голосу з WAV»** — вибрати файл, ввести назву, «Зберегти голос».
Голос одразу зʼявляється у списку (група «Клоновані», кнопка видалення) і доступний в API.

| Метод | Шлях | Опис |
|---|---|---|
| `GET` | `/api/voices` | `{voices: [...], custom: [...], cloning: true}` |
| `POST` | `/api/voices` | `{name, audio_base64}` (WAV) → `{status, voice, overwritten, sample_rate, duration_sec}` |
| `DELETE` | `/api/voices/{name}` | видалити клонований голос (пресет → 400, невідомий → 404) |

- Формат: WAV PCM16/24/32 або float32, будь-яка частота/канали (береться перший канал;
  ресемпл у 24 кГц). Ліміти: ≤60 с, ≤16 МБ; енкодер використовує перші ~30 с
  (як hybrid при `max_sec=30`).
- Назви: `^[\w \-]{1,40}$` (Unicode), не можна займати імена пресетів; повторне
  збереження перезаписує клон.
- Зберігання: `$HOLOS_SPEAKERS_DIR/<name>.npy` — 1-D float32[256], формат numpy
  `np.save`/`np.load`, сумісний з HolosTTS-hybrid. У Docker — writable volume
  `holos_core_speakers`; моделі лишаються read-only, клони переживають
  рестарт/перестворення контейнера.
- `audio_base64` у `/api/synthesize` не підтримується (404): спершу збережіть голос
  через `POST /api/voices`, далі передавайте його імʼя у `voice`.
- Якість/ресурси: int8-енкодер проходить golden-гейт cos ≥0.995 vs fp32
  (фактично 0.99978+); енролмент ~0.8–1.1 с на 5–6 с аудіо (9 потоків), ~8–9 с на 55 с;
  ORT-сесія створюється ліниво (перший енролмент), пік RSS +~0.85 ГБ під час енкоду,
  після — звільняється (арена ORT вимкнена), одночасні енролменти серіалізуються.
- Етика: не клонуйте чужі голоси без згоди.

## API

### `POST /api/synthesize`

| Поле | Тип | Опис |
|---|---|---|
| `text` | str (обовʼязкове) | Текст, 10…50000 символів |
| `voice` | str | Пресет або клонований голос (типово — перший) |
| `speed` | float | Швидкість 0.25–4.0 (типово 1.0) |
| `auto_verbalize` | str/bool | `neural` (типово), `rule`, `off`; legacy `true`/`false` |
| `raw_stress` | bool | `true` → без автонаголосу, власні `+`/U+0301 |
| `format` | str | Лише `wav` (типово); інші формати — 400 |

Відповідь: `{format, sample_rate, duration_sec, voice, text, verbalized_text, audio_base64}`.

### `POST /api/verbalize`

```bash
curl -s http://localhost:8004/api/verbalize \
  -H 'Content-Type: application/json' \
  -d '{"text":"22.08.2025 о 15:30, 250 грн"}'
# {"text":"двадцять другого серпня дві тисячі двадцять пʼятого року о пʼятнадцятій тридцять, ціна двісті пʼятдесят гривень..."}
```

`mode`: `neural` (типово) або `rule`.

### `GET /api/voices`, `GET /healthz`

```bash
curl -s http://localhost:8004/api/voices   # {"voices":[...],"custom":[...],"cloning":true}
curl -s http://localhost:8004/healthz      # статус, version, провайдер, потоки, verbalizer, cloning
```

### `POST /v1/audio/speech` (OpenAI-сумісний)

Формат [OpenAI Audio Speech API](https://platform.openai.com/docs/api-reference/audio/createSpeech) —
працює з клієнтами, що вміють підмінювати `base_url` (SillyTavern, OpenWebUI, LibreChat тощо).

| Поле | Опис |
|---|---|
| `input` | текст (автовербалізація цифр) |
| `voice` | пресет/клон (`Speaker_84`) або імена OpenAI (`alloy`, `nova`, …) |
| `response_format` | `wav` (типово), `pcm`; `mp3` — кодек не підключено (400) |
| `speed` | 0.25–4.0 (типово 1.0) |
| `stream` | `true` → потокова відповідь |

Автентифікація не потрібна; поле `model` приймається й ігнорується.

### `WS /api/ws/synthesize` (потоковий)

Перший кадр — JSON-конфіг, далі бінарні PCM16-чанки (24 кГц, mono), фінальний
текстовий кадр — `{"done": true, "sample_rate": 24000, "duration_sec": ...}`.

```python
import asyncio, json, websockets

async def main():
    async with websockets.connect('ws://localhost:8004/api/ws/synthesize') as ws:
        await ws.send(json.dumps({'text': 'Привіт! Це потоковий синтез мови.', 'voice': 'Speaker_84'}))
        with open('stream.pcm', 'wb') as f:
            async for msg in ws:
                if isinstance(msg, bytes):
                    f.write(msg)
                else:
                    print(json.loads(msg))

asyncio.run(main())
```

Конфіг: `text` (обовʼязково), `voice`, `speed`, `auto_verbalize`, `raw_stress`.
Помилка сервера → текстовий кадр `{"error": "..."}`.

## Конфігурація

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
| `HOLOS_THREADS` | авто | потоки ONNX Runtime (0 = авто: 80% доступних ядер, з урахуванням cgroup-квоти) |
| `HOLOS_METRICS` | `0` | `1` = `[metrics]` логи + `timings` у відповідях (для бенчів) |

### Варіанти збірки (теги образу)

| Тег | Архітектури | Для чого |
|---|---|---|
| `latest` (**дефолт**) | linux/amd64 + linux/arm64 | x86-64 (перевірено на Xeon E5-2650, AVX без AVX2), Raspberry Pi 4/5, ARM-сервери, Apple Silicon (Docker) |
| `sha-<короткий>` | linux/amd64 + linux/arm64 | пін конкретного коміту |
| `vX.Y.Z` (напр. `0.6.1`) | linux/amd64 + linux/arm64 | пін релізу |

ARM64 збирається нативно (`ubuntu-24.04-arm`, без QEMU), маніфест публікується лише
після runtime-смоуку arm64 у CI. Windows ARM64 — у дорожній карті.

Ліміти контейнера (`cpus: "${HOLOS_CPUS:-12.8}"` — 80% 16-ядерного хоста, `memory: 3g`) задані у `docker-compose.yml` — правте
під свій хост. Для rule-only конфігу задайте `HOLOS_VERBALIZER_DIR=""` у environment
(економія ~0.6 ГБ RSS).

## Моделі

Усі моделі — у HF-репозиторії **[Morkon/holos-native-models](https://huggingface.co/Morkon/holos-native-models)**
(публічні; репозиторій ~1.8 ГБ, але типовий набір — ~0.6 ГБ; fp32/legacy/CT2-бекапи
вантажаться лише за потреби):

| Компонент | Шлях у репо | Застосунок |
|---|---|---|
| TTS ONNX int8 + vocab + voices | `tts/` | генерація аудіо |
| Voice encoder ONNX int8 (80 МБ) | `tts/voice_encoder.onnx` | клонування голосу (24 кГц → 256-dim) |
| Neural verbalizer Gemma-3-270M int8 (дефолт) | `verbalizer/gemma-int8/` | цифри→слова (якість) |
| Neural verbalizer M2M100 int8 (legacy) | `verbalizer/onnx-int8/` | відкат, `--with-m2m100` |
| Neural verbalizer CT2 int8 (legacy) | `verbalizer/ct2-int8/` | той самий, CTranslate2 |
| Accentor (ONNX, single-file) | `accentor/` | нейро-фолбек наголосу |
| Stress trie | `stress/stress.trie` | словниковий наголос |

```bash
pip install huggingface_hub
python3 scripts/download_models.py --out models
# legacy M2M100 (+~0.7 ГБ, для відкату): --with-m2m100
```

## Архітектура

```text
текст → препроцес → вербалізація (Gemma-3-270M ONNX int8 або rule)
      → стрес (словник marisa-trie → accentor-фолбек для OOV)
      → фонемізація (ipa_uk) → токенізація → генерація (ONNX/ORT) → PCM/WAV

клон: WAV → 24 кГц resample → voice encoder (ONNX int8) → speakers/<назва>.npy
      → синтез за іменем голосу (пресети та клони в одному просторі ембеддінгів)
```

**Стрес — словниковий + neural фолбек.** `stress.trie` (marisa-trie, 2.9М ключів)
покриває більшість форм за мікросекунди. Слова поза словником обробляє
**accentor-lite** — LSTM, експортований в ONNX (single-file, ~1.2 МБ), той самий
`ukrainian_accentor`, що й у Python-конвеєрі. Unicode-регістр нормалізується utf8proc
(паритет з Python `word.lower()/title()`).

**Вербалізація — два режими** (`auto_verbalize`):

| Режим | Реалізація | Латентність | Якість |
|---|---|---|---|
| `neural` (типово) | Gemma-3-270M (український verbalizer), ONNX int8, KV-cache | ~1–3 с на речення (CPU) | дослівний паритет з hybrid (4/4 MATCH) |
| `rule` | C++ регулярки (цифри/дати/час) | мілісекунди | базовий (граматика відмінювання гірша) |
| `off` / `false` | текст як є | 0 | — |

## Бенчмарки (Xeon 16 ядер, `HOLOS_THREADS=9`, теплий старт, медіана з 3)

Cold start (процес → `healthz` 200): **~10 с** — engine (trie + TTS ONNX + accentor)
~8.0 с, neural verbalizer (Gemma-3-270M) ~2.4 с; rule-only ≈ 8 с.

| Текст | Режим | Частин | TTFB, мс (stream / WS) | total, мс | Аудіо, с | RTF | vb, мс | ort, мс |
|---|---|---|---|---|---|---|---|---|
| короткий, без цифр (18) | rule | 1 | 608 / 584 | 590 | 1.38 | 0.44 | 0 | 584 |
| короткий, без цифр (18) | neural | 1 | 586 / 571 | 583 | 1.38 | 0.43 | 0 | 578 |
| середній, без цифр (86) | rule | 1 | 2116 / 2087 | 2117 | 6.12 | 0.35 | 0 | 2099 |
| цифри/дата (82) | rule | 1 | 3103 / 3061 | 3124 | 9.07 | 0.34 | 0 | 3102 |
| цифри/дата (82) | neural | 1 | 5597 / 5775 | 5625 | 8.75 | 0.64 | 2635 | 2953 |
| довгий (340, 2 частини) | rule | 2 | 4756 / 4729 | 8510 | 24.98 | 0.34 | 0 | 8447 |
| довгий (340, 2 частини) | neural | 2 | 8278 / 7909 | 11183 | 25.20 | 0.44 | 2709 | 8393 |

- `TTFB` — `/v1/audio/speech` (stream) та `WS /api/ws/synthesize`; `total` — `/api/synthesize`.
- Neural-вербалізація працює лише на текстах із цифрами (гейт `has_digits`); без цифр neural ≡ rule.
- RSS: neural **1.07 ГБ**, rule-only (`HOLOS_VERBALIZER_DIR=""`) **468 МБ**; клонування
  додає пік +~0.85 ГБ під час енролменту (звільняється після).
- Вербалізатор: Gemma-3-270M-uk (ONNX int8, MatMul-only QInt8) замість M2M100-418M —
  диск **764 → 226 МБ**, RSS **1.65 → 1.07 ГБ**, cold start vb **~4.6 → ~2.4 с**,
  golden 46/46 vs Python-референс. M2M100 лишається в HF-репо для відкату.
- Цілі ТЗ (TTFB ≤ 200 мс, RTF < 0.25) на CPU не досягнуті — потрібні дрібніший
  стрімінг і оптимізації ORT (окремий етап).

Відтворення: `HOLOS_METRICS=1 ./build/server/holos-server` +
`python3 scripts/bench_latency.py --url http://127.0.0.1:8004 --runs 3`
(markdown-таблиця + JSON; `--cold-container` для заміру cold start у Docker).

### Паритет з hybrid (виміряно, voice=Speaker_43)

| Текст | hybrid | core | Δ |
|---|---|---|---|
| «Привіт, світе! Це тест синтезу мовлення українською.» | 3.60 с | 3.62 с | +0.02 |
| «Купив 25 машин 12 березня 2025 року о 14:30…» | 8.07 с | 8.07 с | 0.00 |
| «Загальна вартість становить 12 450 гривень…» | 7.55 с | 7.55 с | 0.00 |
| «Науково-дослідний інститут працює над проєктом уже три роки.» | 4.22 с | 4.22 с | 0.00 |

Відомі відмінності: `/api/voices` віддає пресети у порядку `voices.json` + `custom`/`cloning`
(розширення core; hybrid має лише `voices`+`custom`); rule-вербалізатор гірше відмінює
(«дванадцять березня» замість «дванадцятого»).

## Збірка з джерела

Залежності: ONNX Runtime 1.31 (prebuilt tarball), sentencepiece (apt), Drogon,
marisa-trie, utf8proc, pcre2, nlohmann_json, dr_wav (FetchContent).

```bash
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DHOLOS_WITH_ORT=ON \
    -DHOLOS_WITH_SERVER=ON \
    -DHOLOS_WITH_VERBALIZER=ON \
    -DHOLOS_ORT_ROOT=/opt/ort-1.31.0
cmake --build build --parallel
```

Golden-тести (пропускаються, якщо шлях не задано):

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
не повʼязана з проєктом). CI збирає конфіг `ORT=ON, SERVER=OFF`, проганяє тести без
моделей, а `docker.yml` збирає amd64+arm64, проганяє arm64-смоук і публікує маніфест
(`latest`, `sha-*`, `v*`).

Запуск нативного бінарника:

```bash
export HOLOS_MODELS_DIR=$PWD/models
export HOLOS_VERBALIZER_DIR=$PWD/models/verbalizer/gemma-int8   # або "" для rule-only
./build/server/holos-server   # :8004
```

## Структура

```
src/core/      бібліотека ядра (текст, вербалізація, стрес, фонеми, ONNX, аудіо)
src/cli/       holos-cli
server/        Drogon HTTP/WS (:8004)
scripts/       download_models.py (HF → models/)
tools/         допоміжні скрипти (конвертація int8, паритет PCM, експорт енкодера)
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
- **M6** ✅ паритет стресу: accentor single-file ONNX, Unicode lower/title,
  UTF-8-safe вставка U+0301; тривалість аудіо Δ ≤0.02 с vs hybrid
- **M7** ✅ Gemma-3-270M verbalizer: новий backend (`verbalizer/gemma-int8`),
  авто-детект `model.onnx`, PCRE2-порт digit_router/bignum_spacing, golden 46/46;
  диск −538 МБ, RSS −0.58 ГБ
- **M8** ✅ клонування голосу: voice encoder ONNX int8 (80 МБ, 24 кГц → 256),
  mask-free StyleEncoder export, `POST/DELETE /api/voices`, UI-панель,
  `speakers/*.npy` (numpy/hybrid-сумісні), golden cos 0.99998
- **M9** ✅ ARM64: multi-arch маніфест amd64+arm64 нативними runners (без QEMU),
  runtime-смоук arm64 (healthz + синтез + enroll/delete клону) перед публікацією `latest`

- **M10** ✅ портативні збірки без Docker: Linux x64 AppImage (моделі всередині) +
  tar.gz, Windows x64 zip; tar.gz/zip довантажують моделі, AppImage працює офлайн;
  смоук у CI перед релізом

Надалі:

- **Windows ARM64** портативна збірка — після появи попиту.
- **GPU**: CUDA/TensorRT/Vulkan-провайдери ORT — відкладено (CPU-цілі пріоритетні;
  на GPU очікується RTF < 0.05).
- Граматика rule-вербалізатора, порядок `/api/voices`, CT2-вербалізація,
  fine-tune Gemma на проблемних кейсах (відмінки числівників, складні групи цифр, валюти).

## Ліцензія та подяки

- **Цей репозиторій** (ядро, сервер, UI, оптимізації): [MIT](LICENSE).
- **Нейромережа HolosTTS** (ваги `holos_cpu_int8.onnx`, `voices.json`) належить автору
  **[patriotyk](https://github.com/patriotyk)** і розповсюджується за MIT:
  [github.com/patriotyk/HolosTTS](https://github.com/patriotyk/HolosTTS) ·
  [huggingface.co/patriotyk/HolosTTS](https://huggingface.co/patriotyk/HolosTTS).
  Велика подяка за модель та препроцес-пайплайн! Сервіс завантажує дзеркало ваг з
  [Morkon/holos-native-models](https://huggingface.co/Morkon/holos-native-models).
- **Voice encoder** (клонування) — експорт StyleEncoder з HolosTTS у ONNX int8;
  golden-гейт cos 0.99998 vs Python-референс.
- **Вербалізація**: `verbalizer/gemma-int8` — квантована похідна Gemma 3 270M
  (© Google, [Gemma Terms of Use](https://ai.google.dev/gemma/terms)) та
  [skypro1111/gemma-3-270m-uk-verbalizer](https://huggingface.co/skypro1111/gemma-3-270m-uk-verbalizer);
  legacy M2M100 — [Helsinki-NLP](https://huggingface.co/Helsinki-NLP) /
  [skypro1111](https://huggingface.co/skypro1111).
- **Наголос**: [ukrainian-word-stress](https://github.com/patriotyk/ukrainian-word-stress)
  (MIT, lang-uk) · [ipa-uk](https://github.com/patriotyk/ipa-uk) (MIT, lang-uk) ·
  [ukrainian-accentor](https://github.com/egorsmkv/ukrainian-accentor).
- **Залежності**: ONNX Runtime, Drogon, marisa-trie, utf8proc, PCRE2, nlohmann_json,
  dr_wav — див. `cmake/` і `docker/Dockerfile`.

Проєкт не афілійований з автором моделі; права на нейромережу належать її автору.
