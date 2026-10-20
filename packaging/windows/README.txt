HolosTTS core — portable build (Windows x64, no Docker)
=======================================================

1. Extract the zip to any folder.
2. Download models once (~0.6 GB): right-click download-models.ps1 ->
   "Run with PowerShell" (or: powershell -ExecutionPolicy Bypass -File download-models.ps1)
3. Run holos-tts.bat
   The browser opens http://127.0.0.1:8004/ (synthesis + voice cloning UI).

Requirements: Windows 10/11 x64, ~2 GB RAM, Microsoft Visual C++ Redistributable
2015-2022 (usually already installed). No installer, no admin rights.
The service listens on 127.0.0.1 only. Change port: set HOLOS_PORT=9000
Expose to LAN: set HOLOS_HOST=0.0.0.0 (no authentication — be careful).

Data:
  models\    model files (override with HOLOS_MODELS_DIR)
  speakers\  cloned voices (*.npy)

CLI: bin\holos-cli.exe
Logs: console window; stop with Ctrl+C.

Do not clone voices without consent. Not affiliated with the model author.
