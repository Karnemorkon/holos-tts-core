# Моделі

Усі моделі — у публічному HF-репозиторії
**[Morkon/holos-native-models](https://huggingface.co/Morkon/holos-native-models)**
(токен не потрібен). Повний репозиторій ~1.8 ГБ, типовий набір — ~0.6 ГБ;
fp32/legacy/CT2-бекапи вантажаться лише за потреби.

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
# примусово перезавантажити: --force
```

У Docker моделі лежать у volume `holos_core_models` (`/app/models`, read-only для
сервера); downloader у `docker-compose.yml` викликає той самий скрипт. Клоновані
голоси зберігаються окремо — у `HOLOS_SPEAKERS_DIR` (volume `holos_core_speakers`).

Шляхи всередині каталогу моделей можна перевизначити змінними `HOLOS_TRIE`,
`HOLOS_VOCAB`, `HOLOS_VOICES`, `HOLOS_ONNX`, `HOLOS_ACCENTOR`,
`HOLOS_VOICE_ENCODER` — див. [CONFIGURATION.md](CONFIGURATION.md).

## Ліцензії

Ваги HolosTTS належать [patriotyk](https://github.com/patriotyk) і розповсюджуються за
MIT. Вербалізатор Gemma 3 270M — © Google, [Gemma Terms of Use](https://ai.google.dev/gemma/terms);
похідна — [skypro1111/gemma-3-270m-uk-verbalizer](https://huggingface.co/skypro1111/gemma-3-270m-uk-verbalizer).
Legacy M2M100 — [Helsinki-NLP](https://huggingface.co/Helsinki-NLP) / [skypro1111](https://huggingface.co/skypro1111).
