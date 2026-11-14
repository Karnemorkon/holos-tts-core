# Implementation Plan: update-app-versions

**Branch**: `001-update-app-versions` | **Date**: 2026-11-14 | **Spec**: specs/001-update-app-versions/spec.md

**Input**: Feature specification from `/specs/001-update-app-versions/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command; its definition describes the execution workflow.

## Summary

Update the Windows and Linux desktop application versions to 0.8.9 so the packaged Windows (zip/portable bundle) and Linux (tar.gz bundle + Tauri AppImage) applications pick up the new 29 CC0 voice catalog released in the core v0.8.9. The core library version is already 0.8.9; this plan ensures every portable distribution artifact reports the same version and ships the new voice catalog without the retired `Speaker_*` names.

## Technical Context

**Language/Version**: C++20 (core library & CLI), Rust (Tauri desktop shell), PowerShell/Bash (packaging scripts)

**Primary Dependencies**: ONNX Runtime 1.31.0 (pinned), Tauri v2 (desktop), Drogon (server)

**Storage**: Models directory carrying `voices.npz`/legacy `voices.json`, ONNX graphs, stress trie, vocab; version embedded in binaries and metadata

**Testing**: `ctest` (core), CLI smoke test (`holos-cli version`), Tauri `cargo check`, portable-build smoke (`smoke_api.py` against version)

**Target Platform**: Windows 10/11 x64 (zip/portable bundle), Linux x64 (tar.gz bundle + AppImage)

**Project Type**: desktop-app + cli + server

**Performance Goals**: N/A (version synchronization; no runtime behavior change)

**Constraints**: Application version MUST equal core project version 0.8.9 per constitution Release Identity; portable builds must produce Linux tar.gz, Linux AppImage, Windows zip, and SHA256SUMS; no `Speaker_*` voices in shipped catalog

**Scale/Scope**: 2 platforms (Windows, Linux); ~6 distribution artifacts

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Constitution Provision | Status | Evidence |
|------------------------|--------|----------|
| Supported Targets — Linux x64, Windows 10/11 x64, Docker linux/amd64+arm64 | PASS | Plan covers Windows & Linux desktop targets; Docker unchanged |
| Release Identity — version synchronized across CMake, Tauri/Cargo/package metadata, Docker docs, README | PASS | Core already at 0.8.9 (merged PR #10); this plan keeps portable artifacts at 0.8.9 |
| v* tag runs Docker multi-arch + portable-build workflows; portable assets + SHA256SUMS produced | PASS | `portable.yml` builds Linux tar.gz, AppImage, Windows zip, SHA256SUMS on tag push |
| Quality gates — Linux build with ORT, ctest, CLI smoke, Tauri cargo check, Docker build | PASS | Existing CI covers; version change adds no new behavior |

**Result**: All gates pass. No violations. Complexity Tracking not required.

## Project Structure

### Documentation (this feature)

```text
specs/001-update-app-versions/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/           # Phase 1 output (/speckit.plan command)
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
packaging/windows/
├── make-bundle.ps1      # Windows portable bundle assembly
├── holos-tts.bat        # Windows launcher
├── download-models.ps1  # Windows model downloader
├── open-ui.ps1          # Windows UI launcher
└── README.txt

packaging/linux/
├── make-bundle.sh       # Linux tar.gz bundle assembly
├── holos-tts.sh         # Linux launcher
├── download-models.sh   # Linux model downloader
└── README.txt

packaging/appimage/
├── make-appimage.sh     # Linux AppImage assembly
└── AppRun               # AppImage entrypoint

desktop/tauri/src-tauri/
├── Cargo.toml           # version = "0.8.9"
├── tauri.conf.json      # version = "0.8.9"
├── package.json         # version = "0.8.9"
├── package-lock.json    # version = "0.8.9"
├── tauri.windows.conf.json
├── binaries/            # staged holos-server sidecar (gitignored target/)
└── src/

CMakeLists.txt           # project VERSION 0.8.9
.github/workflows/portable.yml  # Linux tar.gz + AppImage + Windows zip + SHA256SUMS
```

**Structure Decision**: Single project (DEFAULT). No structural changes needed — the feature only touches version metadata and packaging verification; source tree layout is unchanged.

## Complexity Tracking

> Not applicable — Constitution Check has no violations.

---

## Phase 0: Outline & Research

No `[NEEDS CLARIFICATION]` markers remain in the spec. Edge cases documented in the spec (update prompting, core/app version mismatch) are resolved as assumptions in research below.

### research.md

| Topic | Decision | Rationale | Alternatives considered |
|-------|----------|-----------|-------------------------|
| Windows application version source | Read from `holos-cli version` → CMake `PROJECT_VERSION` (0.8.9) | Single source of truth; already synchronized in core | Hardcoding version in packaging scripts (rejected: drifts from core) |
| Linux application version source | Read from `holos-cli version` → CMake `PROJECT_VERSION` (0.8.9) | Same single source of truth | Hardcoding in Tauri metadata only (rejected: inconsistent with core) |
| Voice catalog inclusion in portable apps | Bundle `voices.npz`/models via `make-bundle.sh`/`.ps1` + `download-*.sh/.ps1` | Models already downloaded at runtime/install; version tag is the trigger | Embedding voices inside the binary (rejected: models are ~0.8 GB, downloaded separately) |
| Update delivery mechanism | Manual obtain of new package (no auto-update for v1) | Spec assumption; keeps scope bounded | Auto-update prompt/notice (rejected: out of scope for v1) |
| Core/app version mismatch handling | Application version == core version (0.8.9) enforced by constitution Release Identity | Prevents mismatched artifacts in circulation | Allowing independent versioning (rejected: violates constitution) |

### Resolved unknowns (from spec edge cases)

- **Update prompting**: Users manually obtain the new Windows/Linux package; no auto-update prompt in v1 (documented assumption).
- **Core/app version mismatch**: Prevented by synchronizing application version to core 0.8.9 across all artifacts per constitution Release Identity.

---

## Phase 1: Design & Contracts

### data-model.md

```markdown
# Data Model: update-app-versions

## Entities

### ApplicationVersion
Represents the release version carried by a desktop application artifact.

| Field | Type | Description | Validation Rule |
|-------|------|-------------|-----------------|
| value | semver string | The application version (e.g., "0.8.9") | MUST equal core project version per constitution Release Identity |
| source | enum | Origin of the version | MUST be CMake `PROJECT_VERSION` (single source of truth) |
| displayed_by | string | Component that shows the version | `holos-cli version`, desktop app shell |

### DistributionArtifact
Represents a packaged form of the application for a platform.

| Field | Type | Description | Validation Rule |
|-------|------|-------------|-----------------|
| platform | enum | Target OS | `windows` or `linux` |
| format | enum | Package type | `zip`, `tar.gz`, `appimage` |
| version | semver string | Artifact version | MUST equal `ApplicationVersion.value` |
| path | filesystem path | Location of the artifact | MUST exist after portable build |
| sha256 | hex string | Checksum for verification | MUST match `SHA256SUMS` entry |
| includes_voice_catalog | boolean | Whether 29 CC0 voices ship | MUST be true; `Speaker_*` MUST be absent |

### VoiceCatalog
Represents the embedded set of voices.

| Field | Type | Description | Validation Rule |
|-------|------|-------------|-----------------|
| count | integer | Number of voices | MUST be 29 |
| licensing | string | Catalog license | MUST be CC0-1.0 |
| includes_retired | boolean | Whether `Speaker_*` present | MUST be false |

## Relationships

- `DistributionArtifact.version` references `ApplicationVersion.value` (1:1 synchronization).
- `DistributionArtifact.includes_voice_catalog` references `VoiceCatalog` (1:1).
- `ApplicationVersion.source` is CMake `PROJECT_VERSION` (1:1 single source).

## State Transitions

- `ArtifactPlanned` → `ArtifactBuilt` → `ArtifactPublished` (on `v*` tag via `portable.yml`).
- A `DistributionArtifact` may only transition to `Published` when its `version` matches `ApplicationVersion.value` and `includes_voice_catalog` is true.
```

### contracts/

The project exposes the following user-facing contracts relevant to this feature:

**`contracts/cli-version.md`** — `holos-cli version` output contract:
- Prints the core project version (currently `0.8.9`).
- Used by portable build scripts (`make-appimage.sh` sets `VERSION` from `holos-cli version`) and by smoke tests to assert the shipped version.

**`contracts/artifact-version.md`** — Portable distribution artifact version contract:
- Every Linux `tar.gz`, Linux `AppImage`, and Windows `zip` artifact file name and embedded version MUST report `0.8.9`.
- `SHA256SUMS` MUST list every published artifact.
- No mismatched version artifacts may circulate.

**`contracts/voice-catalog.md`** — Shipped voice catalog contract:
- The models bundled with Windows and Linux applications MUST contain exactly 29 CC0 voices (`voices.npz` / legacy `voices.json`).
- The retired `Speaker_*` names MUST be absent from every native model format shipped.

### quickstart.md

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
