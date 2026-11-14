# 🎙 v0.8.9 — каталог із 29 CC0-голосів замість виведених `Speaker_*`

Цей реліз замінює вбудований каталог голосів. Старі імена `Speaker_*` більше не працюють — їх прибрано з усіх форматів нативних моделей через відсутність підтвердженої згоди дикторів (обговорення: HolosTTS discussion #5). Ця зміна є несумісною з попередніми версіями (backward-incompatible), тому вона супроводжується чітким керівництвом з міграції: [docs/MIGRATION.md](docs/MIGRATION.md).

## Основні зміни

### 29 CC0-голосів замість 27 `Speaker_*`

У всіх форматах нативних моделей:

| Формат | Файл |
|--------|------|
| бандл | `holos-onnx/voices.npz` |
| legacy | `tts/voices.json` |
| PyTorch | `tts/voices.pt` |

- Описові українські назви голосів (стать, теситура, вік, характер). Ідентифікатори `cv_uk_*` залишаються лише як `legacy_id` у пакеті й не використовуються в API.
- Імена у стилі OpenAI (`alloy`, `echo`, `fable`, `onyx`, `nova`, `shimmer`) продовжують працювати і позиційно зіставляються (маплються) з першими шістьма голосами каталогу.
- Каталог опубліковано під ліцензією **CC0-1.0** у репозиторії [Morkon/holos-voices-uk-cc0](https://huggingface.co/Morkon/holos-voices-uk-cc0); ваги нейромережі HolosTTS залишаються під ліцензією MIT.
- Версія проєкту: **0.8.9** (серія 0.x; тип релізу — minor, з чітким міграційним шляхом).

### Виправлення скрипта завантаження (`scripts/download_models.py`)

- завантажує Gemma-вербалізатор (`verbalizer/gemma-int8`) — чисте встановлення (clean install) більше не стартує в режимі правил (rule-режимі);
- використовує `--repo` для всіх операцій з Hugging Face;
- коректно обробляє `--with-m2m100` як префікс дерева (tree-prefix);
- перевіряє `SHA256SUMS` для бандла;
- валідує `voices.npz` (рівно `names`/`values`, `29 × 256`, `float32`, finite, унікальні імена) та legacy `voices.json` (`29 × 256`, finite);
- має прапорці `--verify` (типово), `--no-verify`, `--verify-only`;
- виводить коректні фінальні підказки для змінних оточення: `HOLOS_MODELS_DIR`, `HOLOS_ONNX`, `HOLOS_VERBALIZER_DIR`.

### Інструменти

- `holos-cli synth` без `--voice` синтезує мовлення першим голосом каталогу (`Жіноче контральто · доросле · рівне`).
- `scripts/bench_latency.py` більше не використовує `Speaker_0`; додано опцію `--voice`.
- `tools/run_pcm_parity.py` позначено як застарілий допоміжний інструмент (legacy helper), тепер приймає голос як аргумент.
- `tests/test_synth.cpp` бере перший голос каталогу, якщо `--voice` не задано.

## Оновлення Docker / портативних збірок

Каталог голосів зберігається в томі (volume) з моделями, а не в самому образі. Після оновлення до v0.8.9 оновіть том моделей:

```bash
docker compose down
docker volume rm holos_core_models   # або запустіть download_models.py --force
docker compose pull
docker compose up -d
```

### Перевірка після оновлення

- `/healthz` → `voices: 29`, `verbalizer: "neural+rule"`, `version: "0.8.9"`
- `/api/voices` → 29 назв, `custom: []`

## Відкат (Rollback)

- **Образ:** поверніться на `morkon06/holostts-core:0.8.8`.
- **Моделі:** потрібна збережена стара копія моделей (volume) із 27 голосами (офіційний репозиторій моделей містить лише 29 CC0-голосів).

## Статус CI

- ✅ CI (Linux build + ctest + CLI smoke)
- ✅ Portable builds (Linux tar.gz, AppImage, Windows zip, SHA256SUMS)
- ✅ Docker (amd64 + arm64 build + smoke)
