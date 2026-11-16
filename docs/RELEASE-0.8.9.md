# 🎙 v0.8.9 — CC0-каталог голосів

Цей реліз замінює вбудований каталог голосів. Старі імена `Speaker_*` більше не
працюють — їх прибрано з усіх форматів нативних моделей через відсутність
підтвердженої згоди дикторів (обговорення:
[HolosTTS discussion #5](https://huggingface.co/patriotyk/HolosTTS/discussions/5)).
Зміна backward-incompatible, тому супроводжується керівництвом з міграції:
[docs/MIGRATION.md](MIGRATION.md).

## Ревізія каталогу (2026-11-16): 17 голосів

Початковий реліз 0.8.9 вийшов із 29 CC0-голосами. 2026-11-16 каталог
перегенеровано, і чинним є **17 голосів**. Номер версії лишається **0.8.9**:
це ревізія вмісту, а не новий реліз.

- Джерело істини — runtime-підкаталоги датасету
  [Morkon/holos-voices-uk-cc0](https://huggingface.co/datasets/Morkon/holos-voices-uk-cc0):
  `onnx/voices.npz`, `onnx-int8/voices.npz`, `mlx/voices.npz` (17 × 256 `float32`,
  ключі `names` + `values`) і `litert/voices.bin` + `litert/holos_litert.json`.
- Файли в корені датасету (`voices.npz`, `voices.bin`, `voices.json`, `voices.pt`,
  `names.json`) — застарілий 29-голосовий набір; завантажувач їх не використовує.
- 12 голосів вилучено, нових не додано. Повний список і таблиця замін —
  [MIGRATION.md](MIGRATION.md).
- `scripts/download_models.py` тепер **завжди** освіжає `voices.npz` (~21 КБ)
  навіть без `--force` і переписує рядок `voices.npz` у `holos-onnx/SHA256SUMS`,
  тому наявні інсталяції та CI-кеші отримують новий каталог. Desktop-лаунчер
  (Tauri) оновлює каталог під час запуску й офлайн працює з локальною копією.
- Legacy `voices.json` генерується з датасетного `voices.npz` (17 записів);
  `tts/voices.json` більше не завантажується з репозиторію ваг.
- Перевірки: `EXPECTED_VOICES = 17` у downloader'і, Docker smoke
  (`voices == 17`), portable smoke; `--verify-only` валідовує 17 × 256.

### 17 CC0-голосів

| Формат | Файл |
|---|---|
| бандл | `holos-onnx/voices.npz` (17 × 256) |
| legacy | `voices.json` (генерується, 17 × 256) |
| LiteRT | `litert/voices.bin` + `litert/holos_litert.json` |

- Перший голос каталогу, типове значення CLI та перший OpenAI-аліас —
  `Жіноче контральто · доросле · темне`.
- OpenAI-імена (`alloy`, `echo`, `fable`, `onyx`, `nova`, `shimmer`) далі
  мапляться позиційно на перші шість голосів каталогу.
- Ліцензія голосів — **CC0-1.0**; ваги HolosTTS — MIT.

### Оновлення Docker / портативних збірок

Каталог голосів зберігається в томі моделей, а не в образі. Достатньо повторно
запустити downloader — він освіжить `voices.npz` навіть без `--force`:

```bash
docker compose run --rm downloader \
  python3 scripts/download_models.py --out models
docker compose restart holos-core
```

Перевірка: `/healthz` → `voices: 17`, `version: "0.8.9"`; `/api/voices` →
17 назв. Для портативних збірок — повторний запуск `download-models.sh` /
`download-models.ps1`. Відкат і подробиці — [MIGRATION.md](MIGRATION.md).

## Початковий реліз 0.8.9 (29 голосів)

### 29 CC0-голосів замість 27 `Speaker_*`

Тоді каталог був однаковий у бандлі (`holos-onnx/voices.npz`), legacy
(`tts/voices.json`) і PyTorch (`tts/voices.pt`).

- Описові українські назви голосів (стать, теситура, вік, характер).
  Ідентифікатори `cv_uk_*` — лише `legacy_id`, не назви для API.
- Каталог опубліковано під **CC0-1.0**; ваги HolosTTS — під MIT.
- Версія проєкту: **0.8.9** (серія 0.x, тип релізу — minor з міграційним шляхом).

### Виправлення скрипта завантаження (`scripts/download_models.py`)

- завантажує Gemma-вербалізатор (`verbalizer/gemma-int8`) — чисте встановлення
  більше не стартує в rule-режимі;
- використовує `--repo` для всіх операцій з Hugging Face;
- коректно обробляє `--with-m2m100` як tree-prefix;
- перевіряє `SHA256SUMS` для бандла;
- валідує `voices.npz` (рівно `names`/`values`, `float32`, finite, унікальні
  імена) та legacy `voices.json`; у поточній ревізії — `17 × 256`;
- має прапорці `--verify` (типово), `--no-verify`, `--verify-only`;
- підтримує `--voices-url` / `HOLOS_VOICES_URL` для іншого джерела каталогу;
- виводить коректні підказки для `HOLOS_MODELS_DIR`, `HOLOS_ONNX`,
  `HOLOS_VERBALIZER_DIR`.

### Інструменти

- `holos-cli synth` без `--voice` синтезує першим голосом каталогу
  (`Жіноче контральто · доросле · темне`).
- `scripts/bench_latency.py` більше не використовує `Speaker_0`; є `--voice`.
- `tools/run_pcm_parity.py` — застарілий legacy-хелпер, приймає голос аргументом.
- `tests/test_synth.cpp` бере перший голос каталогу, якщо `--voice` не задано.

## Перевірка після оновлення

- `/healthz` → `voices: 17`, `verbalizer: "neural+rule"`, `version: "0.8.9"`
- `/api/voices` → 17 назв, `custom: []`

## Відкат (Rollback)

- **Каталог 29 голосів:** відновіть бекап тому моделей або тимчасово передайте
  старий файл через `--voices-url` + `--no-verify` (див.
  [MIGRATION.md](MIGRATION.md)).
- **Образ 0.8.8:** повертає на каталог із 27 `Speaker_*`; це інша, старіша
  гілка каталогу, не 29 CC0.

## Статус CI

- ✅ CI (Linux build + ctest + CLI smoke)
- ✅ Portable builds (Linux tar.gz, AppImage, Windows zip, SHA256SUMS)
- ✅ Docker (amd64 + arm64 build + smoke)

Ревізія каталогу проходить ті самі workflow; Docker smoke перевіряє
`voices == 17` і синтез голосом `Жіноче контральто · доросле · темне`, portable
smoke — 17 голосів.
