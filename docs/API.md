# API

Базовий URL: `http://localhost:8004`. Автентифікації немає — не відкривайте порт у
публічну мережу без reverse proxy. Тіло запитів і відповідей — JSON (`Content-Type:
application/json`), якщо не вказано інше.

## `POST /api/synthesize`

| Поле | Тип | Опис |
|---|---|---|
| `text` | str (обовʼязкове) | Текст, 10…50000 символів |
| `voice` | str | Пресет або клонований голос (типово — перший) |
| `speed` | float | Швидкість 0.25–4.0 (типово 1.0) |
| `auto_verbalize` | str/bool | `neural` (типово), `rule`, `off`; legacy `true`/`false` |
| `raw_stress` | bool | `true` → без автонаголосу, власні `+`/U+0301 |
| `format` | str | Лише `wav` (типово); інші формати — 400 |

Відповідь: `{format, sample_rate, duration_sec, voice, text, verbalized_text, audio_base64}`.

```bash
curl -s -X POST http://localhost:8004/api/synthesize \
  -H 'Content-Type: application/json' \
  -d '{"text":"Привіт, світе! Це тест синтезу мовлення українською.","voice":"Speaker_43"}' \
  | python3 -c 'import sys,json,base64; open("out.wav","wb").write(base64.b64decode(json.load(sys.stdin)["audio_base64"]))'
```

## `POST /api/verbalize`

```bash
curl -s http://localhost:8004/api/verbalize \
  -H 'Content-Type: application/json' \
  -d '{"text":"22.08.2025 о 15:30, 250 грн"}'
# {"text":"двадцять другого серпня дві тисячі двадцять пʼятого року о пʼятнадцятій тридцять, ціна двісті пʼятдесят гривень..."}
```

`mode`: `neural` (типово) або `rule`.

## `GET /api/voices`, `GET /healthz`

```bash
curl -s http://localhost:8004/api/voices   # {"voices":[...],"custom":[...],"cloning":true}
curl -s http://localhost:8004/healthz      # статус, version, провайдер, потоки, verbalizer, cloning
```

## `POST /v1/audio/speech` (OpenAI-сумісний)

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

```bash
curl -s -X POST http://localhost:8004/v1/audio/speech \
  -H 'Content-Type: application/json' \
  -d '{"model":"tts-1","input":"Привіт!","voice":"Speaker_43","response_format":"wav"}' \
  -o speech.wav
```

`stream: true` вмикає chunked-відповідь. Для стрімінгу з custom-схемою є WebSocket.

## `WS /api/ws/synthesize` (потоковий)

Перший кадр — JSON-конфіг, далі бінарні PCM16-чанки (24 кГц, mono), фінальний
текстовий кадр — `{"done": true, "sample_rate": 24000, "duration_sec": ...}`.

Конфіг: `text` (обовʼязково), `voice`, `speed`, `auto_verbalize`, `raw_stress`.
Помилка сервера → текстовий кадр `{"error": "..."}`.

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

## Помилки

| Код | Коли |
|---|---|
| `400` | невалідний `text`/`speed`/`format`, спроба видалити пресет, `mp3` |
| `404` | невідомий голос, `audio_base64` у `/api/synthesize` |
| `500` | помилка синтезу/моделі — деталі в логах контейнера |

Див. також [конфігурацію](CONFIGURATION.md) і [бенчмарки](BENCHMARKS.md).
