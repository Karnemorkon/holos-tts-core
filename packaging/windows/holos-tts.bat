@echo off
setlocal
set "DIR=%~dp0"

if not defined HOLOS_MODELS_DIR set "HOLOS_MODELS_DIR=%DIR%models"
if not defined HOLOS_VERBALIZER_DIR set "HOLOS_VERBALIZER_DIR=%HOLOS_MODELS_DIR%\verbalizer\gemma-int8"
if not defined HOLOS_SPEAKERS_DIR set "HOLOS_SPEAKERS_DIR=%DIR%speakers"
if not defined HOLOS_HOST set "HOLOS_HOST=127.0.0.1"
if not defined HOLOS_PORT set "HOLOS_PORT=8004"

if not exist "%HOLOS_MODELS_DIR%\holos_cpu_int8.onnx" goto nomodels
if not exist "%HOLOS_SPEAKERS_DIR%" mkdir "%HOLOS_SPEAKERS_DIR%"

if not "%HOLOS_OPEN_BROWSER%"=="0" (
    start "" /b powershell -NoProfile -ExecutionPolicy Bypass -File "%DIR%open-ui.ps1"
)

echo HolosTTS: http://%HOLOS_HOST%:%HOLOS_PORT%/  (Ctrl+C to stop)
"%DIR%bin\holos-server.exe"
goto :eof

:nomodels
echo Models not found in %HOLOS_MODELS_DIR%
echo Run download-models.ps1 first (right-click - Run with PowerShell).
pause
exit /b 1
