# Міграція на каталог 29 CC0-голосів (0.8.9)

## Коротко

У версії **0.8.9** вбудований каталог голосів повністю замінено:

- було: **27** службових голосів `Speaker_0` … `Speaker_84` (без підтвердженої
  згоди дикторів);
- стало: **29** голосів із описовими українськими назвами, опублікованих під
  **CC0-1.0** у паку
  [Morkon/holos-voices-uk-cc0](https://huggingface.co/Morkon/holos-voices-uk-cc0).

Новий каталог однаковий у всіх форматах нативних моделей
[Morkon/holos-native-models](https://huggingface.co/Morkon/holos-native-models):

| Формат | Файл | Голосів |
|---|---|---|
| Bundle (`holos-onnx`) | `holos-onnx/voices.npz` | 29 × 256 float32 |
| Legacy (single graph) | `tts/voices.json` | 29 × 256 float32 |
| PyTorch | `tts/voices.pt` | 29 × 256 float32 |

Старих `Speaker_*` більше немає в жодному форматі. Це усвідомлена
backward-incompatible заміна. Проєкт ще в серії 0.x (публічний контракт
нестабільний), тому вона виходить як **0.8.9** з окремим міграційним
документом; від 1.0.0 подібна зміна вимагала б MAJOR-релізу.

## Чому каталог замінено

Для 27 старих голосів `Speaker_*` немає підтвердженої згоди дикторів на
використання їхніх голосів. Це питання публічно піднято в обговоренні
[HolosTTS discussion #5](https://huggingface.co/patriotyk/HolosTTS/discussions/5).
Продовжувати розповсюджувати ці голоси некоректно, тому вони прибрані з
native-моделей, а не перейменовані чи приховані.

Новий пак зібрано з голосів, для яких згода підтверджена; сам пак
розповсюджується за **CC0-1.0**. Ваги нейромережі HolosTTS лишаються під MIT.

## Як оновити клієнт

1. Отримайте актуальний список із `GET /api/voices`:

   ```bash
   curl -s http://localhost:8004/api/voices | python3 -m json.tool
   # {"voices": ["Жіноче контральто · доросле · рівне", ...], "custom": [], "cloning": true}
   ```

2. Виберіть голос **за описом** (стать, теситура, вік, характер) і передавайте
   його точну назву в полі `voice`. Назва — чутлива до регістру, пробілів і
   розділювача `·` (U+00B7 MIDDLE DOT).

3. Якщо у вас був захардкоджений `Speaker_*`, його треба замінити. **Прямого
   відповідника 1:1 немає** — старі голоси не мали публічних описів. Виберіть
   найближчий за статтю та характером із таблиці нижче.

4. Перевірте синтез і, за потреби, оновіть golden/regression-фікстури: тембр і
   тривалість аудіо для того самого тексту зміняться.

Старий ідентифікатор `cv_uk_*` — це лише `legacy_id` з паку. Він **не**
використовується як назва голосу в API; передавайте повну описову назву.

## Каталог 29 голосів

Порядок — як у `GET /api/voices` (сортування за UTF-8). Саме цей порядок
використовується для позиційних OpenAI-аліасів.

| № | Назва в API | `legacy_id` | Група | Опис |
|---:|---|---|---|---|
| 1 | Жіноче контральто · доросле · рівне | `cv_uk_f07` | Жіночі · 30-ті | Adult female contralto, even |
| 2 | Жіноче контральто · доросле · темне | `cv_uk_f26` | Жіночі · 30-ті | Adult female contralto, dark |
| 3 | Жіноче контральто · доросле · яскраве | `cv_uk_f29` | Жіночі · 30-ті | Adult female contralto, bright |
| 4 | Жіноче мецо-сопрано · доросле · яскраве | `cv_uk_f01` | Жіночі · 30-ті | Adult female mezzo-soprano, bright |
| 5 | Жіноче мецо-сопрано · зріле · рівне | `cv_uk_f28` | Жіночі · 40-ві | Mature female mezzo-soprano, even |
| 6 | Жіноче мецо-сопрано · молоде · ясне | `cv_uk_f24` | Жіночі · 20-ті | Young female mezzo-soprano, light |
| 7 | Жіноче мецо-сопрано · старше · тепле | `cv_uk_f05` | Жіночі · 60-ті | Senior female mezzo-soprano, warm |
| 8 | Жіноче сопрано · високе · дуже чисте | `cv_uk_f10` | Жіночі · — | High female soprano, very clear |
| 9 | Жіноче сопрано · доросле · найвище | `cv_uk_f32` | Жіночі · 30-ті | Adult female soprano, highest |
| 10 | Жіноче сопрано · доросле · тепле | `cv_uk_f17` | Жіночі · 30-ті | Adult female soprano, warm |
| 11 | Жіноче сопрано · доросле · яскраве | `cv_uk_f20` | Жіночі · 30-ті | Adult female soprano, bright |
| 12 | Жіноче сопрано · зріле · дуже високе | `cv_uk_f22` | Жіночі · 40-ві | Mature female soprano, very high |
| 13 | Жіноче сопрано · молоде · дуже високе | `cv_uk_f12` | Жіночі · 20-ті | Young female soprano, very high |
| 14 | Жіноче сопрано · молоде · ясне | `cv_uk_f08` | Жіночі · 20-ті | Young female soprano, light |
| 15 | Чоловічий баритон · дорослий · оксамитовий | `cv_uk_m08` | Чоловічі · 30-ті | Adult male baritone, velvety |
| 16 | Чоловічий баритон · дорослий · яскравий | `cv_uk_m30` | Чоловічі · 30-ті | Adult male baritone, bright |
| 17 | Чоловічий баритон · зрілий · теплий | `cv_uk_m01` | Чоловічі · 40-ві | Mature male baritone, warm |
| 18 | Чоловічий баритон · молодий · дзвінкий | `cv_uk_m17` | Чоловічі · 20-ті | Young male baritone, ringing |
| 19 | Чоловічий баритон · молодий · рівний | `cv_uk_m32` | Чоловічі · 20-ті | Young male baritone, even |
| 20 | Чоловічий бас · дорослий · дзвінкий | `cv_uk_m03` | Чоловічі · 30-ті | Adult male bass, ringing |
| 21 | Чоловічий бас · дорослий · найнижчий | `cv_uk_m33` | Чоловічі · 30-ті | Adult male bass, lowest |
| 22 | Чоловічий бас · молодий · рівний | `cv_uk_m02` | Чоловічі · 20-ті | Young male bass, even |
| 23 | Чоловічий тенор · старший · чистий | `cv_uk_m09` | Чоловічі · 50-ті | Senior male tenor, clear |
| 24 | Юнацький голос · дівчина · високий · дзвінкий | `cv_uk_k02` | Юнацькі · підлітки | Teen girl, high, ringing |
| 25 | Юнацький голос · дівчина · високий · теплий | `cv_uk_k33` | Юнацькі · підлітки | Teen girl, high, warm |
| 26 | Юнацький голос · дівчина · дуже високий · яскравий | `cv_uk_k12` | Юнацькі · підлітки | Teen girl, very high, bright |
| 27 | Юнацький голос · середній · теплий | `cv_uk_k27` | Юнацькі · підлітки | Teen voice, mid, warm |
| 28 | Юнацький голос · хлопець · низький · шорсткий | `cv_uk_k29` | Юнацькі · підлітки | Teen boy, low, rough |
| 29 | Юнацький голос · хлопець · середній · теплий | `cv_uk_k08` | Юнацькі · підлітки | Teen boy, mid, warm |

> Примітка щодо `k33` (`Юнацький голос · дівчина · високий · теплий`): у паку
> він зберігається як `raw`. `app_postprocess` із `tempo 1.05` застосовується навколо ядра, а не сервером `holos-tts-core`.

## OpenAI-аліаси

OpenAI-імена далі працюють, але мапляться **позиційно** на перші шість голосів
поточного каталогу (поведінка `pick_voice` у `server/src/main.cpp`):

| OpenAI `voice` | Фактичний голос |
|---|---|
| `alloy` | Жіноче контральто · доросле · рівне |
| `echo` | Жіноче контральто · доросле · темне |
| `fable` | Жіноче контральто · доросле · яскраве |
| `onyx` | Жіноче мецо-сопрано · доросле · яскраве |
| `nova` | Жіноче мецо-сопрано · зріле · рівне |
| `shimmer` | Жіноче мецо-сопрано · молоде · ясне |

Це не семантична відповідність старим голосам, а лише сумісність із клієнтами,
які передають стандартні OpenAI-імена. Для контрольованого тембру передавайте
повну назву голосу.

## Приклади до/після

### REST `POST /api/synthesize`

Було:

```bash
curl -s -X POST http://localhost:8004/api/synthesize \
  -H 'Content-Type: application/json' \
  -d '{"text":"Привіт, світе!","voice":"Speaker_43"}'
```

Стало:

```bash
curl -s -X POST http://localhost:8004/api/synthesize \
  -H 'Content-Type: application/json' \
  -d '{"text":"Привіт, світе!","voice":"Жіноче контральто · доросле · рівне"}'
```

Без `voice` сервер і далі вибирає перший голос каталогу — тепер це
`Жіноче контральто · доросле · рівне`.

### OpenAI `POST /v1/audio/speech`

Було:

```bash
curl -s -X POST http://localhost:8004/v1/audio/speech \
  -H 'Content-Type: application/json' \
  -d '{"model":"tts-1","input":"Привіт!","voice":"Speaker_43","response_format":"wav"}' \
  -o speech.wav
```

Стало (явна назва):

```bash
curl -s -X POST http://localhost:8004/v1/audio/speech \
  -H 'Content-Type: application/json' \
  -d '{"model":"tts-1","input":"Привіт!","voice":"Жіноче контральто · доросле · рівне","response_format":"wav"}' \
  -o speech.wav
```

Або сумісний варіант без зміни клієнта: `"voice":"alloy"` (див. таблицю
аліасів вище).

### WebSocket `/api/ws/synthesize`

Було:

```python
await ws.send(json.dumps({"text": "Привіт!", "voice": "Speaker_84"}))
```

Стало:

```python
await ws.send(json.dumps({
    "text": "Привіт!",
    "voice": "Жіноче сопрано · доросле · тепле",
}))
```

### Діалог `POST /api/synthesize_dialogue`

Було:

```bash
-d '{"segments":[
  {"text":"Привіт, я Олена.","voice":"Speaker_0"},
  {"text":"Привіт, я Іван.","voice":"Speaker_1","start_offset_ms":1200}
]}'
```

Стало:

```bash
-d '{"segments":[
  {"text":"Привіт, я Олена.","voice":"Жіноче контральто · доросле · рівне"},
  {"text":"Привіт, я Іван.","voice":"Чоловічий баритон · зрілий · теплий","start_offset_ms":1200}
]}'
```

## Оновлення Docker і портативних збірок

Каталог голосів живе в моделях, а не в образі. Після оновлення до 0.8.9
треба оновити volume моделей:

```bash
# Docker compose: перезавантажити моделі в наявний volume
docker compose run --rm downloader \
  python3 scripts/download_models.py --out models --force

# або видалити volume і дати downloader'у завантажити все заново
docker compose down
docker volume rm holos_core_models
docker compose up -d
```

Для портативних збірок і нативного запуску — те саме: або
`download_models.py --force`, або видаліть каталог `models/` і запустіть
завантаження знову. Старий volume з 27 голосами не містить `voices.npz`
нового каталогу; без `--force` downloader вважатиме файли наявними.

Перевірка після оновлення:

```bash
curl -s http://localhost:8004/healthz | python3 -m json.tool
# "voices": 29, "model_format": "bundle", "verbalizer": "neural+rule"

curl -s http://localhost:8004/api/voices
# 29 назв, "custom": []
```

### Відкат

- **Образ:** поверніться на `morkon06/holostts-core:0.8.8`.
- **Моделі:** потрібен збережений старий volume/копія моделей із 27 голосами.
  Офіційний `Morkon/holos-native-models` уже містить лише 29 CC0-голосів, тому
  «відкотити» моделі повторним завантаженням не вийде.
- Якщо старий volume було перезаписано (`--force`) і копії немає, відкат до
  старих `Speaker_*` неможливий — лишається тільки 0.8.9 з 29 голосами.

Перед оновленням production-інсталяції зробіть бекап volume:

```bash
docker run --rm -v holos_core_models:/models -v "$PWD:/backup" alpine \
  tar czf /backup/holos-models-0.8.8.tar.gz -C /models .
```

## Дивись також

- [API](API.md) — повний контракт REST/OpenAI/WebSocket.
- [Моделі](MODELS.md) — формати, `voices.npz`, ліцензії.
- [Release 0.8.9](RELEASE-0.8.9.md) — стислий список змін релізу.
