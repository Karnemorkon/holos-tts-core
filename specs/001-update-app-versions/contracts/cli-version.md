# Contracts: update-app-versions

The project exposes the following user-facing contracts relevant to this feature.

## contracts/cli-version.md — `holos-cli version` output contract

`holos-cli version` prints the core project version (currently `0.8.9`).
- Used by portable build scripts (e.g., `packaging/appimage/make-appimage.sh` sets `VERSION` from `holos-cli version`).
- Used by smoke tests to assert the shipped version.
- Output: a single version string, e.g. `0.8.9`.

## contracts/artifact-version.md — Portable distribution artifact version contract

Every published portable artifact MUST report version `0.8.9`:
- Linux `tar.gz` file name contains `0.8.9` (e.g., `holos-tts-core-0.8.9-linux-x64.tar.gz`).
- Linux `AppImage` file name contains `0.8.9`.
- Windows `zip` file name contains `0.8.9`.
- `SHA256SUMS` lists every published artifact with matching checksums.
- No mismatched version artifacts may circulate.

## contracts/voice-catalog.md — Shipped voice catalog contract

The models bundled with Windows and Linux applications MUST satisfy:
- Exactly 29 voices (`voices.npz` / legacy `voices.json`).
- Licensed under CC0-1.0.
- The retired `Speaker_*` names are absent from every native model format shipped.
