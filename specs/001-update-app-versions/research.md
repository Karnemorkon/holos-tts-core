# Research: update-app-versions

**Feature**: update-app-versions (specs/001-update-app-versions/spec.md)
**Date**: 2026-11-14

## Context

The core library is already at version 0.8.9 (merged PR #10). This research resolves how the Windows and Linux desktop applications pick up that version and the new 29 CC0 voice catalog.

## Decisions

### Decision: Application version source
**Decision**: The Windows and Linux application versions are derived from `holos-cli version`, which reads CMake `PROJECT_VERSION` (0.8.9). Packaging scripts do not hardcode a version.
**Rationale**: Single source of truth; the core project version is already synchronized across CMake, Tauri/Cargo/package metadata, Docker docs, and README per constitution Release Identity. Hardcoding would risk drift between core and desktop app versions.
**Alternatives considered**:
- Hardcode version in each packaging script — rejected: would diverge from core version over time.
- Read version from Tauri metadata only — rejected: inconsistent with core CLI and Linux bundle.

### Decision: Voice catalog inclusion
**Decision**: The 29 CC0 voice catalog ships via the models directory (`voices.npz` / legacy `voices.json`) downloaded by `download-*.sh/.ps1` and bundled by `make-bundle.sh`/`.ps1`. Voices are not embedded inside the binary.
**Rationale**: Model files are ~0.8 GB and downloaded separately at install/runtime; the version tag governs which catalog the downloader fetches.
**Alternatives considered**:
- Embed voices inside the application binary — rejected: models are too large to embed; breaks the existing download model.

### Decision: Update delivery mechanism
**Decision**: Users manually obtain the new Windows/Linux package; no auto-update prompt in v1.
**Rationale**: Documented assumption in the spec; keeps scope bounded to version synchronization.
**Alternatives considered**:
- Auto-update prompt/notice — rejected: out of scope for v1.

### Decision: Core/application version mismatch handling
**Decision**: Application version is enforced to equal core version 0.8.9 across all artifacts per constitution Release Identity; mismatched artifacts cannot circulate.
**Rationale**: Constitution requires version synchronization across all release artifacts.
**Alternatives considered**:
- Allow independent versioning of desktop app vs core — rejected: violates constitution Release Identity.

## Resolved unknowns (from spec edge cases)

| Edge case | Resolution |
|-----------|------------|
| Are users prompted to update, or manually obtain the new package? | Manually obtain (no auto-update in v1) — documented assumption. |
| How is a core/app version mismatch handled? | Prevented by synchronizing application version to core 0.8.9 per constitution Release Identity. |

## Open items for implementation (Phase 2 / tasks.md)

- Verify `portable.yml` produces Linux `tar.gz`, Linux `AppImage`, and Windows `zip` all carrying `0.8.9` on a `v0.8.9` tag.
- Verify `SHA256SUMS` lists every published artifact and validates.
- Local `cargo check` for Tauri cannot run (missing `gobject-2.0` dev pkg-config); CI covers this check.
- Local Docker build cannot run (Docker unavailable); CI covers Docker `linux/amd64` build.
