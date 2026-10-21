# holos-tts-core

[![Stand With Ukraine](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/banner-direct-single.svg)](https://stand-with-ukraine.pp.ua)
[![Made in Ukraine](https://img.shields.io/badge/made_in-Ukraine-ffd700.svg?labelColor=0057b7)](https://stand-with-ukraine.pp.ua)
[![CI](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/ci.yml)
[![Docker](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/docker.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/docker.yml)
[![Portable](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/portable.yml/badge.svg)](https://github.com/Karnemorkon/holos-tts-core/actions/workflows/portable.yml)
[![Docker Hub](https://img.shields.io/badge/docker-morkon06%2Fholostts--core-blue)](https://hub.docker.com/r/morkon06/holostts-core)
[![Release](https://img.shields.io/github/v/release/Karnemorkon/holos-tts-core)](https://github.com/Karnemorkon/holos-tts-core/releases/latest)

> **English summary:** production-ready Dockerized **Ukrainian text-to-speech** service
> (REST + OpenAI-compatible + WebSocket + Web UI, port 8004) in **C++20 + ONNX Runtime** —
> no Python at runtime. 27 voices, voice cloning, neural verbalization and stress;
> multi-arch `linux/amd64` + `linux/arm64`. Current release: **0.7.1**.

Нативне C++ ядро українського синтезу мовлення: один бінарник + ONNX Runtime, без Python
у рантаймі. Перший запуск довантажує моделі (~0.6 ГБ) у named volume.

<details>
<summary>Відмова від відповідальності / Disclaimer</summary>

* **UA:** Цей проєкт є незалежною аматорською розробкою (нативною C++ обгорткою) і **не є офіційним продуктом** автора оригінальної нейромережі. Він жодним чином не афілійований, не спонсорується і не підтримується розробником [patriotyk](https://github.com/patriotyk). Усі права на оригінальні моделі, ваги та препроцес належать їхньому законному автору. Проєкт надається «як є» (as is), автор цієї обгортки не несе відповідальності за можливі збої чи використання сервісу.
* **EN:** This project is an independent, community-driven native C++ wrapper and **is not an official product** of the original neural network creator. It is not affiliated with, endorsed, or sponsored by [patriotyk](https://github.com/patriotyk). All rights to the original models and weights belong to their respective owner. The software is provided «as is», without warranty of any kind.

</details>

## Можливості

- **Синтез українською** 24 кГц: 27 пресетів голосів + **клонування голосу з WAV**
  (клон зберігається під власним імʼям і стає звичайним `voice` для REST/OpenAI API).
- **Нативний C++20**: Drogon HTTP/WS + ONNX Runtime; без Python, PyTorch і Gradio у рантаймі.
- **Автовербалізація**: `neural` (Gemma-3-270M int8, типово) / `rule` (C++ регулярки) / `off`;
  наголос — словник marisa-trie (2.9 млн форм) + accentor-lite ONNX для слів поза словником.
- **Потоковий синтез**: REST-стрім (`/v1/audio/speech`), WebSocket `/api/ws/synthesize`
  (PCM16-чанки) і вбудований Web UI.
- **API-паритет з HolosTTS-hybrid**: та сама схема запитів/відповідей, тривалість аудіо Δ ≤0.02 с.
- **Docker**: multi-arch образ ~290 МБ (amd64 + arm64), named volumes, healthcheck, ліміти CPU/RAM.
- **Портативні збірки без Docker**: Linux x64 (AppImage з GUI, tar.gz) і Windows x64
  (NSIS-інсталятор, zip); 81 unit/golden-тест.

## Швидкий старт

```bash
git clone https://github.com/Karnemorkon/holos-tts-core.git
cd holos-tts-core
docker compose up -d --build
curl http://localhost:8004/healthz
# Web UI: http://localhost:8004/
# логи першого запуску: docker compose logs -f holos-core
```

Ознака готовності:
`{"status":"ok","engine":"holos-tts-core-native","version":"0.7.1","verbalizer":"neural+rule","cloning":true,...}`.

Потрібен Docker + Compose і ≥2 ГБ RAM (neural-дефолт тримає ~1.1 ГБ RSS; rule-only — ~468 МБ).
Сервіс `downloader` завантажує лише відсутні файли, тож оновлення образу не тягне моделі заново.

Перший синтез (WAV `out.wav`):

```bash
curl -s -X POST http://localhost:8004/v1/audio/speech \
  -H 'Content-Type: application/json' \
  -d '{"input":"Привіт, світе! Це тест синтезу мовлення українською.","voice":"Speaker_43"}' \
  -o out.wav
```

## Docker Hub

Образ: [`morkon06/holostts-core`](https://hub.docker.com/r/morkon06/holostts-core) —
multi-arch **linux/amd64 + linux/arm64**. Публікується лише з тегів `v*`:
`0.7.1` (пін релізу) і `latest`. Тегів `sha-*` більше немає.

Образ **не містить моделей**. Найпростіше — compose з репозиторію: замініть у
`docker-compose.yml` блок `build:` на `image: morkon06/holostts-core:0.7.1`, сервіс
`downloader` довантажить моделі. Повний приклад, ліміти й альтернативи —
[docs/CONFIGURATION.md](docs/CONFIGURATION.md#docker-hub).

## Портативні збірки (без Docker)

Готові файли — у [Releases](https://github.com/Karnemorkon/holos-tts-core/releases):

| Платформа | Файл | Мінімум |
|---|---|---|
| Linux x64, GUI | `holos-tts-core-<версія>-x86_64.AppImage` | Debian 13+/Ubuntu 24.04+, WebKitGTK 4.1, FUSE2 |
| Linux x64 | `holos-tts-core-<версія>-linux-x64.tar.gz` | glibc ≥ 2.35 (Ubuntu 22.04+) |
| Windows x64 | `holos-tts-core-<версія>-windows-x64-setup.exe` / `-windows-x64.zip` | Windows 10/11 + VC++ 2015–2022 |

Усі збірки довантажують моделі (~0.6 ГБ) при першому запуску. Вимоги, DE-нюанси
(GNOME/XFCE/LXQt/тайли), Wayland+NVIDIA і логи — [docs/PORTABLE.md](docs/PORTABLE.md).

## Моделі

Моделі публічні: **[Morkon/holos-native-models](https://huggingface.co/Morkon/holos-native-models)**
(повний репозиторій ~1.8 ГБ, типовий набір — ~0.6 ГБ; fp32/legacy — лише за потреби).

```bash
pip install huggingface_hub
python3 scripts/download_models.py --out models
```

Склад, legacy M2M100 і примусове перезавантаження — [docs/MODELS.md](docs/MODELS.md).

## API

`POST /api/synthesize`, `POST /api/verbalize`, `GET /api/voices`, `POST/DELETE /api/voices`,
`GET /healthz`, OpenAI-сумісний `POST /v1/audio/speech` і WebSocket `/api/ws/synthesize`.
Автентифікації немає; докладно — [docs/API.md](docs/API.md).

## Документація

| Документ | Про що |
|---|---|
| [API](docs/API.md) | REST, OpenAI, WebSocket, клонування голосу |
| [Конфігурація](docs/CONFIGURATION.md) | змінні, теги образів, compose, ліміти |
| [Моделі](docs/MODELS.md) | склад, завантаження, legacy-вербалізатор |
| [Архітектура](docs/ARCHITECTURE.md) | пайплайн, стрес, вербалізація, структура |
| [Бенчмарки](docs/BENCHMARKS.md) | латентність, RSS, паритет з hybrid, методика |
| [Збірка з джерела](docs/BUILDING.md) | cmake, golden-тести, нативний запуск |
| [Портативні збірки](docs/PORTABLE.md) | AppImage, tar.gz, Windows, DE-нюанси |
| [Дорожня карта](docs/ROADMAP.md) | M0–M10 і подальші плани |

## Ліцензія та подяки

- **Цей репозиторій** (ядро, сервер, UI, оптимізації): [MIT](LICENSE).
- **Нейромережа HolosTTS** (ваги `holos_cpu_int8.onnx`, `voices.json`) належить автору
  **[patriotyk](https://github.com/patriotyk)** і розповсюджується за MIT:
  [github.com/patriotyk/HolosTTS](https://github.com/patriotyk/HolosTTS) ·
  [huggingface.co/patriotyk/HolosTTS](https://huggingface.co/patriotyk/HolosTTS).
  Сервіс завантажує дзеркало ваг з [Morkon/holos-native-models](https://huggingface.co/Morkon/holos-native-models).
- **Вербалізація**: `verbalizer/gemma-int8` — квантована похідна Gemma 3 270M
  (© Google, [Gemma Terms of Use](https://ai.google.dev/gemma/terms)) та
  [skypro1111/gemma-3-270m-uk-verbalizer](https://huggingface.co/skypro1111/gemma-3-270m-uk-verbalizer);
  legacy M2M100 — [Helsinki-NLP](https://huggingface.co/Helsinki-NLP) / [skypro1111](https://huggingface.co/skypro1111).
- **Наголос**: [ukrainian-word-stress](https://github.com/patriotyk/ukrainian-word-stress),
  [ipa-uk](https://github.com/patriotyk/ipa-uk), [ukrainian-accentor](https://github.com/egorsmkv/ukrainian-accentor).
- **Залежності**: ONNX Runtime, Drogon, marisa-trie, utf8proc, PCRE2, nlohmann_json,
  dr_wav — див. `cmake/` і `docker/Dockerfile`.

Проєкт не афілійований з автором моделі; права на нейромережу належать її автору.
