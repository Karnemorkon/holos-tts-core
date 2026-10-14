# holos-tts-core

C++ native core for **HolosTTS** (Ukrainian text-to-speech). Наступний крок
дорожньої карти (Етап 3 ТЗ): заміна Python-сервісу легшим C++ ядром.

> **Статус: M0 (каркас).** Реалізовано словниковий стрес-модуль (de-risk).
> Порт інших модулів, HTTP-паритет і Docker/CI-публікація — M1–M3.

## Цілі (ТЗ Етап 3)

- RAM ≤ 400 МБ (проти ~1.5 ГБ у Python)
- Cold start < 0.3 с, **Time-To-First-Byte ≤ 200 мс**
- API-паритет із `Karnemorkon/HolosTTS-hybrid` (Wyoming-проксі працює без змін)
- Автономна збірка без Python (Linux; Windows/Android NDK — надалі)

## Архітектура пайплайну

```
текст → препроцес → вербалізація(M2M100/CT2) → стрес(словник marisa-trie)
      → фонемізація(ipa_uk) → токенізація → генерація(ONNX/ORT) → PCM/WAV
```

**Стрес — словниковий, без нейро (варіант A).** A/B-дослідження показало:
нейро-модель ByT5 (CT2) на CPU має латентність 6–17 с/речення і поступається
точністю словниковому підходу. Натомість `stress.trie` (marisa-trie) дає
**98.7%** словникових форм з одним наголосом за мікросекунди, без жодної
нейромережі. Неоднознані та OOV слова залишаються без наголосу (TTS застосовує
дефолтну вимову) — відповідає `on_ambiguity=Skip` у Python.

## Залежності

| Залежність | Призначення | Спосіб |
|---|---|---|
| ONNX Runtime 1.31 | генерація аудіо (`holos_cpu_int8.onnx`) | prebuilt tarball |
| CTranslate2 4.8 | вербалізація M2M100 (M1) | FetchContent (source) |
| sentencepiece | токенізатор M2M100 (M1) | FetchContent |
| marisa-trie | словник наголосів `stress.trie` | FetchContent |
| Drogon | HTTP/WS сервер (M2) | FetchContent |
| nlohmann_json, dr_wav | утиліти | FetchContent |

## Збірка

```bash
# локально (ORT вимкнено для швидкої перевірки каркаса)
cmake --preset default -DHOLOS_WITH_ORT=OFF
cmake --build build --parallel
./build/holos-cli version

# словниковий стрес (потрібен stress.trie з ukrainian-word-stress)
./build/holos-cli stress /path/to/stress.trie вода місто привіт

# повна збірка (ORT + CT2 + сервер) — M1+
cmake --preset full -DHOLOS_ORT_ROOT=/opt/ort
cmake --build build-full --parallel
```

### Docker

```bash
docker build -f docker/Dockerfile -t holos-tts-core:local .
```

Багато-архітектурна збірка та публікація на Docker Hub (`morkon06/holostts-core`)
налаштовуються у M3 (buildx amd64+arm64).

## Структура

```
src/core/      бібліотека ядра (текст, стрес, фонеми, ONNX, аудіо)
src/cli/       holos-cli (інструмент рядка)
server/        Drogon HTTP/WS (M2)
tools/         допоміжні скрипти (export_voices.py тощо)
tests/         unit + golden-тести (golden PCM проти Python — M1)
cmake/         FetchContent-залежності
docker/        multi-stage Dockerfile
```

## Дорожня карта

- **M0** ✅ каркас: CMake, словниковий стрес-модуль (de-risk), Dockerfile, CI
- **M1** порт ядра: текст-преп, ipa_uk (+golden), токенізатор, ORT-сесія, CLI-синтез
- **M2** HTTP-паритет на Drogon (:8004), стрім, WS
- **M3** Docker/CI amd64+arm64 → Hub, бенчі (RAM/TTFB), перемикання продакшену

## Ліцензія

MIT. Моделі та `stress.trie` належать їх авторам (див. `patriotyk/*`,
`ukrainian-word-stress`).
