# Quickstart Validation: update-app-versions

## Prerequisites
- Core project at version 0.8.9 (CMake `PROJECT_VERSION`).
- ONNX Runtime 1.31.0 available (CI or local `/tmp/opencode/onnxruntime-linux-x64-1.31.0`).
- Tauri toolchain for AppImage (CI covers; local `cargo check` blocked by missing `gobject-2.0`).

## Validation Scenarios

### Scenario 1: Verify core and metadata versions are 0.8.9
**Setup**: Repository checked out at `main` after PR #10 merge.
**Run**:
```bash
grep 'VERSION 0.8.9' CMakeLists.txt
grep '"version": "0.8.9"' desktop/tauri/src-tauri/Cargo.toml desktop/tauri/src-tauri/tauri.conf.json desktop/tauri/package.json
```
**Expected**: All files report `0.8.9`.

### Scenario 2: Verify `holos-cli version` reports 0.8.9
**Setup**: Build core with `HOLOS_WITH_SERVER=OFF`.
**Run**:
```bash
./build/holos-cli version
```
**Expected**: Output `0.8.9`.

### Scenario 3: Verify portable Linux bundle carries 0.8.9 and 29 voices
**Setup**: Run `portable.yml` Linux job (or locally: cmake build + install + `packaging/linux/make-bundle.sh` + model download).
**Run**:
```bash
VERSION="$(dist/holos-tts-core/bin/holos-cli version)"
tar -tzf "holos-tts-core-${VERSION}-linux-x64.tar.gz" | grep -c voices.npz
```
**Expected**: `VERSION` = `0.8.9`; `voices.npz` present (1).

### Scenario 4: Verify Windows bundle carries 0.8.9 and 29 voices
**Setup**: Run `portable.yml` Windows job (CI; local Windows toolchain unavailable).
**Run**: Inspect produced `holos-tts-core-*-win64.zip` / `make-bundle.ps1` output.
**Expected**: `holos-cli.exe version` = `0.8.9`; `voices.npz` present; no `Speaker_*` names.

### Scenario 5: Verify no mismatched artifacts
**Setup**: List artifacts from `portable.yml` for a `v0.8.9` tag.
**Run**: Confirm every artifact file name contains `0.8.9` and `SHA256SUMS` matches.
**Expected**: All versions consistent; `SHA256SUMS` verifies.

## Expected Outcomes
- Windows application reports version 0.8.9 and ships the 29 CC0 voice catalog.
- Linux application reports version 0.8.9 and ships the 29 CC0 voice catalog.
- All Windows and Linux artifacts share the same version (0.8.9).
- Retired `Speaker_*` voices are absent from both updated applications.
