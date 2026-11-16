# Моделі

Ваги — у публічному HF-репозиторії
**[Morkon/holos-native-models](https://huggingface.co/Morkon/holos-native-models)**,
каталог голосів — в окремому датасеті
**[Morkon/holos-voices-uk-cc0](https://huggingface.co/datasets/Morkon/holos-voices-uk-cc0)**
(токен не потрібен). Повний репозиторій ваг ~1.8 ГБ, типовий набір — ~0.6 ГБ;
fp32/legacy/CT2-бекапи вантажаться лише за потреби.

| Компонент | Шлях у репо | Застосунок |
|---|---|---|
| Багатоетапний бандл holos-onnx int8 (дефолт) | `holos-onnx-int8/` | генерація аудіо (encoder/prosody/decoder + streaming) |
| Каталог голосів (17 CC0) | датасет `holos-voices-uk-cc0/onnx/voices.npz` | пресети голосів |
| TTS ONNX int8 + vocab (legacy) | `tts/` | генерація аудіо одним графом; `voices.json` генерується з датасету |
| Voice encoder ONNX int8 (80 МБ) | `tts/voice_encoder.onnx` | клонування голосу (24 кГц → 256-dim) |
| Neural verbalizer Gemma-3-270M int8 (дефолт) | `verbalizer/gemma-int8/` | цифри→слова (якість) |
| Neural verbalizer M2M100 int8 (legacy) | `verbalizer/onnx-int8/` | відкат, `--with-m2m100` |
| Neural verbalizer CT2 int8 (legacy) | `verbalizer/ct2-int8/` | той самий, CTranslate2 |
| Accentor (ONNX, single-file) | `accentor/` | нейро-фолбек наголосу |
| Stress trie | `stress/stress.trie` | словниковий наголос |

```bash
pip install huggingface_hub
python3 scripts/download_models.py --out models --format bundle   # дефолт у compose
python3 scripts/download_models.py --out models --format legacy  # старий однографовий
# legacy M2M100 (+~0.7 ГБ, для відкату): --with-m2m100
# примусово перезавантажити: --force
# перевірити SHA256SUMS і voices.npz/voices.json без завантаження: --verify-only
# вимкнути перевірку (не рекомендується): --no-verify
```

## Каталог голосів

Вбудований каталог — **17 CC0-голосів** з описовими українськими назвами.
Джерело істини — runtime-підкаталоги датасету
[Morkon/holos-voices-uk-cc0](https://huggingface.co/datasets/Morkon/holos-voices-uk-cc0):

| Runtime | Файл | Формат |
|---|---|---|
| `onnx/` | `voices.npz` | 17 × 256 `float32`, ключі `names` + `values` |
| `onnx-int8/` | `voices.npz` | те саме |
| `mlx/` | `voices.npz` | те саме |
| `litert/` | `voices.bin` | сирі `float32` підряд, 17 × 256 |
| `litert/` | `holos_litert.json` | оновлений `voices.names` |

> **Увага:** файли в **корені** датасету (`voices.npz`, `voices.bin`,
> `voices.json`, `voices.pt`, `names.json`) — це застарілий 29-голосовий набір.
> Не використовуйте їх; завантажувач бере лише runtime-підкаталоги.

`voices.npz` містить рівно два масиви: `names.npy` (`<U…`) і `values.npy`
(`<f4`, shape `(17, 256)`); жодних `cv_uk_*` у рантаймі немає. Повний список і
міграція зі старого каталогу — [MIGRATION.md](MIGRATION.md).

`scripts/download_models.py` оновлює каталог голосів **завжди** (файл ~21 КБ),
навіть якщо решта моделей уже наявна, і переписує рядок `voices.npz` у
`holos-onnx/SHA256SUMS`. Desktop-застосунок (Tauri) робить те саме під час
запуску; офлайн він працює з локальною копією. Перевизначити джерело можна
через `--voices-url` або `HOLOS_VOICES_URL`.

## Формати моделей

`HOLOS_MODEL_FORMAT` (або `SynthesizerMode` у C++) вибирає рушій:

| Значення | Поведінка |
|---|---|
| `auto` (дефолт) | бандл, якщо в `HOLOS_ONNX` є `holos_onnx.json` (format_version 2); інакше legacy-граф |
| `bundle` | тільки багатоетапний бандл; помилка, якщо маніфесту немає |
| `legacy` | тільки старий однографовий `holos_cpu_int8.onnx` |

Бандл — це каталог `models/holos-onnx/` (задається `HOLOS_ONNX`):

```
holos-onnx/
  holos_onnx.json     # маніфест v2: етапи, sample_rate, samples_per_frame,
                      # max_frames, vocab, streaming{chunk,context,window_frames}
  encoder.onnx        # токени+голос+темп -> acoustic/prosodic/durations
  prosody.onnx        # -> f0, energy (2 значення на кадр)
  decoder.onnx        # exact-режим: повний кадровий ряд -> аудіо
  decoder_window.onnx # streaming: вікно кадрів (+ source) -> аудіо-чанк
  source.onnx         # f0 -> збудження для window-декодера
  voices.npz          # 17 CC0-голосів (names.npy + values.npy, 256-dim)
```

`voices.npz` читається нативно (ZIP64 + DEFLATE через zlib, `.npy`-парсер), без
конвертації у JSON. Для бандла голоси й фонемний словник беруться **з самого
бандла**, а не з `voices.json`/`vocab.json`: ембеддінги іншого voice-енкодера
дають іншу вимову й темп. `HOLOS_VOICES` впливає лише на legacy-режим; bundle
ігнорує цю змінну й читає `$HOLOS_ONNX/voices.npz`.

### Сумісність клонованих голосів

Клонований голос — це 256-dim ембеддінг від `voice_encoder.onnx`. Якщо енкодер
оновлювався (файл у `tts/voice_encoder.onnx`), старі `speakers/*.npy` стають
несумісними: голос звучатиме чужорідно. Такі голоси треба завантажити заново
через UI/`POST /api/voices`; пресети з бандла (`/api/voices`) завжди актуальні.

У Docker моделі лежать у volume `holos_core_models` (`/app/models`, read-only для
сервера); downloader у `docker-compose.yml` викликає той самий скрипт. Клоновані
голоси зберігаються окремо — у `HOLOS_SPEAKERS_DIR` (volume `holos_core_speakers`).

Шляхи всередині каталогу моделей можна перевизначити змінними `HOLOS_TRIE`,
`HOLOS_VOCAB`, `HOLOS_VOICES`, `HOLOS_ONNX`, `HOLOS_ACCENTOR`,
`HOLOS_VOICE_ENCODER`, `HOLOS_MODEL_FORMAT` — див.
[CONFIGURATION.md](CONFIGURATION.md).

## Ліцензії

Ваги HolosTTS належать [patriotyk](https://github.com/patriotyk) і розповсюджуються за
MIT. Вбудовані голоси — **CC0-1.0**, датасет
[Morkon/holos-voices-uk-cc0](https://huggingface.co/datasets/Morkon/holos-voices-uk-cc0).
Вербалізатор Gemma 3 270M — © Google, [Gemma Terms of Use](https://ai.google.dev/gemma/terms);
похідна — [skypro1111/gemma-3-270m-uk-verbalizer](https://huggingface.co/skypro1111/gemma-3-270m-uk-verbalizer).
Legacy M2M100 — [Helsinki-NLP](https://huggingface.co/Helsinki-NLP) / [skypro1111](https://huggingface.co/skypro1111).
