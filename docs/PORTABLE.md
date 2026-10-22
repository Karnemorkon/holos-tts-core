# Портативні збірки

Готові файли — у [Releases](https://github.com/Karnemorkon/holos-tts-core/releases).
Усі збірки при першому запуску довантажують моделі (~0.6 ГБ) і не потребують Docker.

| Платформа | Файл | Мінімум |
|---|---|---|
| Linux x64, GUI | `holos-tts-core-<версія>-x86_64.AppImage` | Debian 13+/Ubuntu 24.04+, glibc ≥ 2.39, WebKitGTK 4.1, FUSE2 |
| Linux x64 | `holos-tts-core-<версія>-linux-x64.tar.gz` | glibc ≥ 2.35 (Ubuntu 22.04+) |
| Windows x64 | `holos-tts-core-<версія>-windows-x64-setup.exe` / `-windows-x64.zip` | Windows 10/11 + VC++ Redistributable 2015–2022 |

## Linux AppImage (GUI)

```bash
chmod +x holos-tts-core-0.7.1-x86_64.AppImage
./holos-tts-core-0.7.1-x86_64.AppImage
```

- Відкриває нативне вікно HolosTTS; перший запуск завантажує моделі (~0.6 ГБ)
  у `${XDG_DATA_HOME:-~/.local/share}/holos-tts-core/models` з прогресом і кнопкою
  «Повторити» у разі помилки.
- Потрібен системний **WebKitGTK 4.1** (Debian/Ubuntu: `libwebkit2gtk-4.1-0`; зазвичай
  уже встановлений). WebKit/GTK/GStreamer в AppImage **не** входять.
- Потрібен **FUSE2**: Debian/Ubuntu — `libfuse2t64`, Arch — `fuse2`, Fedora — `fuse-libs`.
  Без FUSE: `APPIMAGE_EXTRACT_AND_RUN=1 ./holos-tts-core-0.7.1-x86_64.AppImage`.
- Закриття вікна зупиняє сервер; логи сайдкара —
  `~/.local/share/holos-tts-core/logs/sidecar.log`.
- `HOLOS_PORT` — фіксований порт; `XDG_DATA_HOME` — інший каталог даних.

### Особливості середовищ

- **GNOME, Cinnamon, XFCE**: звичайне GTK3-вікно (типово 1100×780, мінімум 720×560),
  системний GTK-файловий діалог; трею та глобального меню немає.
- **LXQt**: застосунок GTK у Qt-сесії, файловий діалог виглядатиме чужорідно. Якщо
  `zenity` не встановлено, діалог помилки AppRun може не зʼявитися — запускайте з
  термінала, щоб побачити повідомлення.
- **Тайлові WM (i3/sway/Hyprland тощо)**: мінімальний розмір вікна може ігноруватися,
  а файловий діалог — тайлитися. Додайте floating-правило для вікна HolosTTS.
- **Wayland + NVIDIA (proprietary)**: у разі порожнього вікна або артефактів спробуйте
  `WEBKIT_DISABLE_DMABUF_RENDERER=1 ./holos-tts-core-0.7.1-x86_64.AppImage`.
- **NixOS**: запускайте через `appimage-run` або з `APPIMAGE_EXTRACT_AND_RUN=1`.
- Автоматичний смоук виконується під Xvfb; візуальна поведінка залежить від
  системного WebKitGTK і конкретного DE.

## Linux tar.gz

```bash
tar xzf holos-tts-core-0.7.1-linux-x64.tar.gz
cd holos-tts-core
./download-models.sh
./holos-tts.sh          # UI: http://127.0.0.1:8004/
```

Моделі та клоновані голоси лежать у `models/` і `speakers/` поруч із лаунчером, тож
каталог можна переносити цілком.

## Windows x64

Потрібен Visual C++ Redistributable 2015–2022.

### Інсталятор

```powershell
.\holos-tts-core-0.7.1-windows-x64-setup.exe
```

Інсталятор ставить застосунок у каталог користувача, створює ярлики та одразу
запускає GUI. DLL ONNX Runtime лежать **поруч із** `HolosTTS.exe` і
`holos-server.exe`, тому старі `onnxruntime.dll` із `PATH` або робочого каталогу
не підхоплюються.

### Портативний ZIP

```powershell
Expand-Archive holos-tts-core-0.7.1-windows-x64.zip -DestinationPath .
cd holos-tts-core
powershell -ExecutionPolicy Bypass -File download-models.ps1
holos-tts.bat
```

### Faugus / Wine (експериментально)

Windows-збірку можна запускати через Faugus (Flatpak) або інший Proton/Wine.
Якщо після оновлення лишається помилка `API version [31] ... 1.17.1`, створіть
чистий префікс або перевірте `drive_c` на застарілий `onnxruntime.dll`: новий
setup кладе ORT 1.31 поруч з exe, і саме він має завантажуватися.

## Мережа

Типово сервіс слухає `127.0.0.1:8004` (AppImage обирає вільний порт, `HOLOS_PORT`
фіксує його). `HOLOS_HOST=0.0.0.0` відкриває доступ для LAN — **без автентифікації**.
Типово використовується neural verbalizer і 80% ядер CPU для ONNX Runtime.

## Діагностика

| Симптом | Що зробити |
|---|---|
| Вікно не відкривається | запустити з термінала; перевірити `logs/sidecar.log`, WebKitGTK 4.1 і FUSE2 |
| `error loading libfuse.so.2` | встановити FUSE2 або `APPIMAGE_EXTRACT_AND_RUN=1` |
| Порожнє вікно / артефакти (Wayland+NVIDIA) | `WEBKIT_DISABLE_DMABUF_RENDERER=1` |
| Моделі не завантажуються | кнопка «Повторити», перевірити мережу та доступ до HF, логи |
| Порт зайнятий | задати інший `HOLOS_PORT` |
| Немає звуку | перевірити PipeWire/PulseAudio і вибраний вихід |
| `The requested API version [31] ... 1.17.1` (Windows) | перевстановити setup від 2026-10-22; видалити старий каталог інсталяції; переконатися, що `onnxruntime.dll` лежить поруч із `holos-server.exe` |
| Windows-збірка у Faugus/Wine не бачить ORT 1.31 | створити чистий префікс або прибрати старий `onnxruntime.dll` із `drive_c`; DLL уже лежать поруч з exe |
