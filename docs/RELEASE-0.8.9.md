# Реліз 0.8.9

**Дата:** 2026-11-14

Реліз замінює вбудований каталог голосів: старі імена `Speaker_*` більше не
працюють. Попри те, що проєкт ще в серії 0.x (публічний контракт нестабільний),
це backward-incompatible зміна, тому вона супроводжується явним міграційним
документом. Деталі та приклади — [docs/MIGRATION.md](MIGRATION.md).

## Головне

- **29 CC0-голосів** замість 27 `Speaker_*` у всіх форматах native-моделей
  (`holos-onnx/voices.npz`, `tts/voices.json`, `tts/voices.pt`).
- Описові українські назви; `cv_uk_*` лишаються тільки як `legacy_id` у паку і
  не використовуються в API.
- OpenAI-імена `alloy`, `echo`, `fable`, `onyx`, `nova`, `shimmer` далі
  працюють і мапляться позиційно на перші 6 голосів каталогу.
- Каталог опубліковано в
  [Morkon/holos-voices-uk-cc0](https://huggingface.co/Morkon/holos-voices-uk-cc0)
  під **CC0-1.0**; ваги моделі лишаються під MIT.

## Виправлення downloader'а

`scripts/download_models.py`:

- завантажує Gemma-вербалізатор (`verbalizer/gemma-int8`) — чистий install
  більше не стартує в `rule`-режимі;
- використовує `--repo` для всіх операцій з HF;
- коректно обробляє `--with-m2m100` як tree-prefix;
- перевіряє `SHA256SUMS` для бандла;
- перевіряє `voices.npz` (рівно `names`/`values`, 29 × 256, float32, finite,
  унікальні імена) і legacy `voices.json` (29 × 256 finite);
- має `--verify` (типово), `--no-verify`, `--verify-only`;
- друкує коректні фінальні підказки `HOLOS_MODELS_DIR`, `HOLOS_ONNX`,
  `HOLOS_VERBALIZER_DIR`.

## Інструменти

- `holos-cli synth` без `--voice` синтезує першим голосом каталогу.
- `scripts/bench_latency.py` більше не використовує `Speaker_0`; додано
  `--voice` (типово — вибір сервера).
- `tools/run_pcm_parity.py` приймає голос аргументом і позначений як
  застарілий legacy-хелпер.
- `tests/test_synth.cpp` бере перший голос каталогу, якщо `--voice` не задано.

## Оновлення

```bash
docker compose down
docker volume rm holos_core_models    # або download_models.py --force
docker compose pull
docker compose up -d
```

Після старту: `/healthz` → `voices: 29`, `verbalizer: "neural+rule"`;
`/api/voices` → 29 назв, `custom: []`.
