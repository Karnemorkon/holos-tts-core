# API

Базовий URL: `http://localhost:8004`. Автентифікації немає — не відкривайте порт у
публічну мережу без reverse proxy. Тіло запитів і відповідей — JSON (`Content-Type:
application/json`), якщо не вказано інше.

Вбудований каталог — **29 CC0-голосів** з описовими українськими назвами. Точний
список завжди доступний у `GET /api/voices`; назви чутливі до регістру, пробілів і
розділювача `·`. Старі `Speaker_*` більше не працюють — див.
[MIGRATION.md](MIGRATION.md).

## `POST /api/synthesize`

| Поле | Тип | Опис |
|---|---|---|
| `text` | str (обовʼязкове) | Текст, 10…50000 символів |
| `voice` | str | Назва пресета з `/api/voices` або клонований голос (типово — перший голос каталогу) |
| `speed` | float | Швидкість 0.25–4.0 (типово 1.0) |
| `auto_verbalize` | str/bool | `neural` (типово), `rule`, `off`; legacy `true`/`false` |
| `raw_stress` | bool | `true` → без автонаголосу (лише власні `+`/U+0301); `false` (типово) → автонаголос, а `+` у тексті також підтримується |
| `format` | str | Лише `wav` (типово); інші формати — 400 |

Відповідь: `{format, sample_rate, duration_sec, voice, text, verbalized_text, audio_base64}`.

```bash
curl -s -X POST http://localhost:8004/api/synthesize \
  -H 'Content-Type: application/json' \
  -d '{"text":"Привіт, світе! Це тест синтезу мовлення українською.","voice":"Жіноче контральто · доросле · рівне"}' \
  | python3 -c 'import sys,json,base64; open("out.wav","wb").write(base64.b64decode(json.load(sys.stdin)["audio_base64"]))'
```

## `POST /api/synthesize_dialogue`

Мульти-спікерний синтез: масив реплік різними голосами в одному WAV.

| Поле | Тип | Опис |
|---|---|---|
| `segments` | array (обовʼязкове) | 1…200 реплік |
| `segments[].text` | str | Текст репліки (мінімальної довжини немає) |
| `segments[].voice` | str | Назва пресета з `/api/voices` або клонований голос (типово — перший голос каталогу) |
| `segments[].speed` | float | Швидкість 0.25–4.0 (типово 1.0) |
| `segments[].auto_verbalize` | str/bool | `neural` (типово), `rule`, `off` |
| `segments[].raw_stress` | bool | `true` → без автонаголосу |
| `segments[].start_offset_ms` | int | Абсолютний початок репліки від початку аудіо (0…600000). Без поля репліка йде послідовно за попередньою; перекриття змішуються (поліфонія) |
| `format` | str | Лише `wav` (типово) |

Сумарний текст усіх реплік — до 50000 символів.

Відповідь: `{format, sample_rate, duration_sec, segments: [{index, voice, speed, text,
verbalized_text, duration_sec, start_offset_ms, raw_stress, auto_verbalize}], audio_base64}`.

```bash
curl -s -X POST http://localhost:8004/api/synthesize_dialogue \
  -H 'Content-Type: application/json' \
  -d '{"segments":[{"text":"Привіт, я Олена.","voice":"Жіноче контральто · доросле · рівне"},
                   {"text":"Привіт, я Іван.","voice":"Чоловічий баритон · зрілий · теплий","start_offset_ms":1200}]}' \
  | python3 -c 'import sys,json,base64; open("dialog.wav","wb").write(base64.b64decode(json.load(sys.stdin)["audio_base64"]))'
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
curl -s http://localhost:8004/healthz      # статус, version, провайдер, потоки, verbalizer, stress (dictionary-accentor), cloning,
                                           # model_format (bundle|legacy), sample_rate,
                                           # samples_per_frame, max_frames, streaming
```

## `POST /v1/audio/speech` (OpenAI-сумісний)

Формат [OpenAI Audio Speech API](https://platform.openai.com/docs/api-reference/audio/createSpeech) —
працює з клієнтами, що вміють підмінювати `base_url` (SillyTavern, OpenWebUI, LibreChat тощо).

| Поле | Опис |
|---|---|
| `input` | текст (автовербалізація цифр) |
| `voice` | назва пресета з `/api/voices` (`Жіноче контральто · доросле · рівне`), клон (`My Voice`) або ім'я OpenAI (`alloy`, `nova`, …) |
| `response_format` | `wav` (типово), `pcm`; `mp3` — кодек не підключено (400) |
| `speed` | 0.25–4.0 (типово 1.0) |
| `stream` | `true` → потокова відповідь |

Автентифікація не потрібна; поле `model` приймається й ігнорується.

OpenAI-імена `alloy`, `echo`, `fable`, `onyx`, `nova`, `shimmer` мапляться
**позиційно** на перші шість голосів каталогу в порядку `/api/voices`. Для
контрольованого тембру передавайте повну назву голосу.

```bash
curl -s -X POST http://localhost:8004/v1/audio/speech \
  -H 'Content-Type: application/json' \
  -d '{"model":"tts-1","input":"Привіт!","voice":"Жіноче контральто · доросле · рівне","response_format":"wav"}' \
  -o speech.wav
```

`stream: true` вмикає chunked-відповідь. Для стрімінгу з custom-схемою є WebSocket.

## `WS /api/ws/synthesize` (потоковий)

Перший кадр — JSON-конфіг, далі бінарні PCM16-чанки (24 кГц, mono), фінальний
текстовий кадр — `{"done": true, "sample_rate": 24000, "duration_sec": ...}`.

Конфіг: `text` (обовʼязково), `voice`, `speed`, `auto_verbalize`, `raw_stress`.
Помилка сервера → текстовий кадр `{"error": "..."}`.

Діалог: замість `text` передайте `{"dialogue": true, "segments": [...]}` (ті самі поля, що
в HTTP-ендпоінті). Репліки синтезуються послідовно в один потік; додатково надсилається
прогрес сегмента `{"progress": {"phase": "segment", "current": 2, "total": 3}}`.

```python
import asyncio, json, websockets

async def main():
    async with websockets.connect('ws://localhost:8004/api/ws/synthesize') as ws:
        await ws.send(json.dumps({'text': 'Привіт! Це потоковий синтез мови.', 'voice': 'Жіноче сопрано · доросле · тепле'}))
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

## Словник наголосів користувача

UI: вкладка **«Наголоси»** — таблиця записів, поле для додавання (`ка+ва`), видалення
із підтвердженням. Зміни застосовуються одразу, без перезапуску сервера.

| Метод | Шлях | Опис |
|---|---|---|
| `GET` | `/api/stress-user` | `{path, count, words: [{raw, word, stressed}]}`; коментарі й порожні рядки не повертаються |
| `POST` | `/api/stress-user` | `{word: "ка+ва"}` → додає/замінює запис, `{status, path, count, words}` |
| `DELETE` | `/api/stress-user/{word}` | видаляє записи слова (коментарі зберігаються), `{status, path, count, words}` |

- Валідація: одне слово без пробілів, `+` дає рівно один наголос («прощаюча»
  обробка як у тексті: `к+ава` → `кáва`); помилка → `400` з `detail` українською.
- Запис у файл атомарний (`.tmp` + rename); після кожної зміни словник
  перечитується на місці (живий reload, синтез не переривається).
- `DELETE` невідомого слова → `404`; шлях не задано (`HOLOS_STRESS_USER`) → `503`.

```bash
curl -s http://localhost:8004/api/stress-user
curl -s -X POST http://localhost:8004/api/stress-user \
  -H 'Content-Type: application/json' -d '{"word":"ка+ва"}'
curl -s -X DELETE http://localhost:8004/api/stress-user/%D0%BA%D0%B0%2B%D0%B2%D0%B0
```

## Помилки

| Код | Коли |
|---|---|
| `400` | невалідний `text`/`speed`/`format`, спроба видалити пресет, `mp3`, невалідний запис наголосу |
| `404` | невідомий голос, `audio_base64` у `/api/synthesize`, невідоме слово у `DELETE /api/stress-user` |
| `500` | помилка синтезу/моделі — деталі в логах контейнера |
| `503` | збереження голосів/словника вимкнено (немає `voice_encoder.onnx` / `HOLOS_STRESS_USER`) |

Див. також [конфігурацію](CONFIGURATION.md) і [бенчмарки](BENCHMARKS.md).
