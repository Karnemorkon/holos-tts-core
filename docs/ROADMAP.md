# Дорожня карта

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
- **M10** ✅ портативні збірки без Docker: Linux x64 AppImage (Tauri GUI) +
  tar.gz, Windows x64 NSIS/zip; усі довантажують моделі при першому запуску;
  смоук у CI перед релізом
- **0.7.1** ✅ AppImage з системним WebKitGTK замість вбудованого; стійкий лаунчер
  (sidecar-лог, retry UI), асинхронне завантаження моделей; Docker Hub лише з тегів `v*`

## Надалі

- **Windows ARM64** портативна збірка — після появи попиту.
- **GPU**: CUDA/TensorRT/Vulkan-провайдери ORT — відкладено (CPU-цілі пріоритетні;
  на GPU очікується RTF < 0.05).
- Уніфікувати типовий `HOLOS_VERBALIZER_DIR` Docker-образу з `gemma-int8` (зараз у
  Dockerfile — legacy `onnx-int8`, compose перевизначає).
- Граматика rule-вербалізатора, порядок `/api/voices`, CT2-вербалізація,
  fine-tune Gemma на проблемних кейсах (відмінки числівників, складні групи цифр, валюти).
