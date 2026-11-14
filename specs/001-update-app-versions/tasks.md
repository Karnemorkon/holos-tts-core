# Tasks: update-app-versions

**Input**: Design documents from `/specs/001-update-app-versions/`

**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: Tests are OPTIONAL - not explicitly requested in the feature specification; no test tasks generated. Validation is covered by quickstart.md scenarios executed in the Polish phase.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and confirmation that the core is already at version 0.8.9 (merged PR #10).

- [ ] T001 Confirm repository is on `main` with PR #10 merged (core project version 0.8.9) — check `git log --oneline -3`
- [ ] T002 Confirm `CMakeLists.txt` reports `project(holos-tts-core VERSION 0.8.9 ...)`
- [ ] T003 [P] Confirm `desktop/tauri/src-tauri/Cargo.toml` reports `version = "0.8.9"`
- [ ] T004 [P] Confirm `desktop/tauri/src-tauri/tauri.conf.json` reports `"version": "0.8.9"`
- [ ] T005 [P] Confirm `desktop/tauri/package.json` and `desktop/tauri/package-lock.json` report version `0.8.9`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented — confirm version source and packaging wiring.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T006 Confirm packaging scripts derive version from `holos-cli version` (not hardcoded): `packaging/appimage/make-appimage.sh`, `packaging/linux/make-bundle.sh`, `packaging/windows/make-bundle.ps1`
- [ ] T007 [P] Confirm `.github/workflows/portable.yml` builds Linux tar.gz, Linux AppImage, and Windows zip on `v*` tag push
- [ ] T008 [P] Confirm `.github/workflows/portable.yml` generates `SHA256SUMS` for all published artifacts

**Checkpoint**: Foundation ready — version is 0.8.9 across core and desktop metadata; portable workflow wires version from `holos-cli version`. User story implementation can now begin in parallel.

---

## Phase 3: User Story 1 - Windows user updates desktop app to get new voices (Priority: P1) 🎯 MVP

**Goal**: Windows desktop application reports version 0.8.9 and ships the 29 CC0 voice catalog without retired `Speaker_*` names.

**Independent Test**: Install the updated Windows package and verify `holos-cli version` outputs `0.8.9` and the voice catalog contains 29 CC0 voices with no `Speaker_*` names.

### Implementation for User Story 1

- [ ] T009 [P] [US1] Verify `packaging/windows/make-bundle.ps1` copies `holos-server.exe` and `holos-cli.exe` from install prefix to `bin/`
- [ ] T010 [P] [US1] Verify `packaging/windows/holos-tts.bat` launcher exists and references correct executable names
- [ ] T011 [P] [US1] Verify `packaging/windows/download-models.ps1` exists to download models at install/runtime
- [ ] T012 [US1] Verify Windows bundle ships `voices.npz` containing exactly 29 voices × 256 float32 values (per contracts/voice-catalog.md)
- [ ] T013 [US1] Verify no `Speaker_*` names exist in Windows bundle voice catalog (`voices.npz` / legacy `voices.json`)
- [ ] T014 [US1] Rebuild Windows portable bundle and confirm `holos-cli.exe version` outputs `0.8.9`
- [ ] T015 [US1] Confirm Windows artifact file name contains `0.8.9` (per contracts/artifact-version.md)

**Checkpoint**: User Story 1 is fully functional and testable independently — Windows app reports 0.8.9 with 29 CC0 voices, no `Speaker_*`.

---

## Phase 4: User Story 2 - Linux user updates desktop app to get new voices (Priority: P1) 🎯 MVP

**Goal**: Linux desktop application reports version 0.8.9 and ships the 29 CC0 voice catalog without retired `Speaker_*` names.

**Independent Test**: Install the updated Linux package (tar.gz bundle or AppImage) and verify `holos-cli version` outputs `0.8.9` and the voice catalog contains 29 CC0 voices with no `Speaker_*` names.

### Implementation for User Story 2

- [ ] T016 [P] [US2] Verify `packaging/linux/make-bundle.sh` copies `holos-server` and `holos-cli` from install prefix to `bin/`
- [ ] T017 [P] [US2] Verify `packaging/linux/holos-tts.sh` launcher exists and references correct executable names
- [ ] T018 [P] [US2] Verify `packaging/linux/download-models.sh` exists to download models at install/runtime
- [ ] T019 [P] [US2] Verify `packaging/appimage/make-appimage.sh` sets `VERSION` from `"$BUNDLE/bin/holos-cli" version` (not hardcoded)
- [ ] T020 [US2] Verify Linux tar.gz bundle ships `voices.npz` containing exactly 29 voices × 256 float32 values (per contracts/voice-catalog.md)
- [ ] T021 [US2] Verify no `Speaker_*` names exist in Linux bundle voice catalog (`voices.npz` / legacy `voices.json`)
- [ ] T022 [US2] Verify AppImage `holos-cli version` outputs `0.8.9`
- [ ] T023 [US2] Confirm Linux artifact file names contain `0.8.9` (per contracts/artifact-version.md)

**Checkpoint**: User Story 2 is fully functional and testable independently — Linux app reports 0.8.9 with 29 CC0 voices, no `Speaker_*`.

---

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories — end-to-end validation and documentation.

- [x] T024 [P] Run quickstart.md Scenario 1: verify core and metadata versions are 0.8.9 (`grep` on CMakeLists.txt, Cargo.toml, tauri.conf.json, package.json)
- [x] T025 [P] Run quickstart.md Scenario 2: verify `./build/holos-cli version` outputs `0.8.9`
- [x] T026 [P] Run quickstart.md Scenario 3: verify portable Linux bundle carries 0.8.9 and `voices.npz` present
- [ ] T027 [P] Run quickstart.md Scenario 4: verify Windows bundle carries 0.8.9 and `voices.npz` present, no `Speaker_*` (CI; local Windows toolchain unavailable)
- [ ] T028 [P] Run quickstart.md Scenario 5: verify no mismatched artifacts — every artifact file name contains `0.8.9` and `SHA256SUMS` matches
- [x] T029 [P] Update `docs/PORTABLE.md` if it references a version other than 0.8.9 for Windows/Linux portable builds
- [ ] T030 Commit all verification/configuration changes on the feature branch

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion — BLOCKS all user stories
- **User Stories (Phase 3, Phase 4)**: All depend on Foundational phase completion
  - User Story 1 (Windows) and User Story 2 (Linux) are independent of each other — can proceed in parallel if staffed
  - Each story independently testable
- **Polish (Phase 5)**: Depends on both User Stories complete

### User Story Dependencies

- **User Story 1 (P1, Windows)**: Can start after Foundational (Phase 2) — no dependencies on other stories
- **User Story 2 (P1, Linux)**: Can start after Foundational (Phase 2) — no dependencies on User Story 1; independently testable

### Within Each User Story

- Confirm packaging script wiring → verify voice catalog → rebuild/re-verify version → confirm artifact naming
- Core implementation before integration checks
- Story complete before moving to Polish phase

### Parallel Opportunities

- All Setup tasks marked [P] can run in parallel (T003, T004, T005)
- All Foundational tasks marked [P] can run in parallel (T007, T008)
- User Story 1 and User Story 2 phases can run in parallel once Foundational is complete
- Within each story, [P] tasks (packaging script verification) can run in parallel
- All Polish tasks marked [P] can run in parallel (T024–T029)

### Parallel Example: User Story 1

```bash
# Launch all packaging script verifications for User Story 1 together:
Task: "Verify packaging/windows/make-bundle.ps1 copies holos-server.exe and holos-cli.exe"   (T009 [P] [US1])
Task: "Verify packaging/windows/holos-tts.bat launcher exists"                               (T010 [P] [US1])
Task: "Verify packaging/windows/download-models.ps1 exists"                                  (T011 [P] [US1])
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL — blocks all stories)
3. Complete Phase 3: User Story 1 (Windows)
4. **STOP and VALIDATE**: Test User Story 1 independently — install Windows package, confirm version 0.8.9 and 29 CC0 voices
5. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Deploy/Demo (MVP!)
3. Add User Story 2 → Test independently → Deploy/Demo
4. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (Windows packaging verification)
   - Developer B: User Story 2 (Linux packaging verification)
3. Stories complete and integrate independently

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Commit after each task or logical group
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- Local `cargo check` for Tauri cannot run (missing `gobject-2.0` dev pkg-config); CI covers this check
- Local Docker build cannot run (Docker unavailable); CI covers Docker `linux/amd64` build
- Local Windows build cannot run on Linux; CI covers Windows portable build
