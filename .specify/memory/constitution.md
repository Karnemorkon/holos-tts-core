<!--
Sync Impact Report
==================
Version change: 1.0.0 → 2.0.0 (MAJOR)
Reason: the built-in voice catalog was intentionally replaced. The retired
27 Speaker_* voices are absent from every native model format and cannot be
preserved; keeping them in the constitution would contradict the shipped
models. The project remains pre-1.0, so the catalog replacement ships as
project version 0.8.9 with an explicit migration guide.

Modified principles:
- II. Voice Catalog and OpenAI API Compatibility — redefined around the
  Morkon/holos-native-models catalog with descriptive names. At the time of
  this amendment the catalog contains 29 CC0 voices, identical in the bundle
  (voices.npz), legacy (voices.json), and PyTorch (voices.pt) formats. The
  retired Speaker_* names are explicitly outside the catalog and are covered
  by docs/MIGRATION.md.

Added sections: none
Removed sections: none
Updated artifacts:
- docs/MIGRATION.md (new migration and rollback guide)
- docs/RELEASE-0.8.9.md (new release notes)
- README.md, docs/API.md, docs/MODELS.md, docs/CONFIGURATION.md,
  docs/BENCHMARKS.md, dockerhub/overview.md
- scripts/download_models.py, scripts/bench_latency.py,
  src/cli/holos_cli.cpp, tests/test_synth.cpp, tools/run_pcm_parity.py
- version 0.8.8 → 0.8.9 in CMake, Cargo, Tauri, package metadata, README,
  and Docker Hub metadata

Follow-up TODOs: none
-->

# HolosTTS Core Constitution

## Core Principles

### I. Native C++20 CPU Runtime (NON-NEGOTIABLE)

All production inference MUST run on the CPU through ONNX Runtime. The core and server MUST be
built as C++20. Shipped runtime artifacts — the server, CLI, desktop application, Docker image,
and portable bundles — MUST NOT require Python, PyTorch, Gradio, CUDA, or any GPU runtime.
Python MAY be used for development, model preparation, and CI tooling, but it MUST NOT be invoked
on a production request path.

Rationale: CPU-only inference keeps the single-binary and container promise, removes a large
operational dependency, and lets the service run on ordinary machines without a GPU or Python.

### II. Voice Catalog and OpenAI API Compatibility (NON-NEGOTIABLE)

The built-in voice catalog MUST be the catalog published in
`Morkon/holos-native-models`, using its descriptive voice names. At the time of this amendment
the catalog contains 29 CC0 voices. The same catalog — identical names and identical
256-dimensional embeddings — MUST be present in every native model format: the bundle
(`voices.npz`), legacy (`voices.json`), and PyTorch (`voices.pt`).

Every release MUST keep all voices of the current catalog available under their existing names,
with their existing behavior. The OpenAI-compatible `POST /v1/audio/speech` endpoint MUST remain
usable by clients that only replace `base_url`. It MUST accept `input`, `voice`,
`response_format`, `speed`, `stream`, and `model` (accepted and ignored) as documented, and MUST
continue to support `wav` and `pcm` responses. OpenAI voice names (`alloy`, `echo`, `fable`,
`onyx`, `nova`, `shimmer`) MUST map positionally to the first six voices of the current catalog.

Voice cloning MAY add new voices, but it MUST NOT remove, rename, or shadow any voice of the
current catalog. The retired `Speaker_*` names are not part of the catalog; their removal is
covered by `docs/MIGRATION.md`. A change that breaks an existing client contract MUST ship with
an explicit migration path. While the project is in the 0.x series, such a change MAY ship in a
MINOR or PATCH release; from 1.0.0 onward it requires a project MAJOR release.

Rationale: users and integrations depend on a stable voice catalog and request/response contract;
silent renames or removals break existing audio pipelines. The catalog is defined by the models
that actually ship, so the constitution must track that catalog instead of preserving retired
names that no longer exist in any native model format.

### III. Windows ONNX Runtime 1.17.1 Compatibility

Windows x64 portable and installer builds MUST load the ONNX Runtime DLL shipped next to the
executable. A Windows machine that has ONNX Runtime 1.17.1 installed, or has its DLL in the process
working directory, MUST still start and synthesize successfully. The Windows smoke test MUST place
an ONNX Runtime 1.17.1 DLL in the working directory and verify that the bundled runtime wins. A
bundled ONNX Runtime upgrade MUST keep this test green.

Rationale: old ONNX Runtime copies on PATH or in the working directory are a real deployment
failure mode; the release must be self-contained and deterministic.

### IV. Tag-Gated Docker Hub Publishing

Only a push of a tag matching `v*` MAY push container image layers or manifests to Docker Hub.
Pull requests and branch workflows MAY build images for validation, but they MUST NOT push images.
Published image tags MUST be the release version and `latest`; `sha-*` image tags MUST NOT be
published. Docker Hub metadata such as the repository README MAY be synchronized from `main`
without publishing an image.

Rationale: tag-gated publishing keeps registry history tied to reviewed releases and prevents
unreleased code from reaching users.

### V. Pull-Request-Only Production Changes (NON-NEGOTIABLE)

Every change to `main` MUST land through a pull request. A pull request MUST NOT be merged until
all required CI checks pass on its latest head commit. Direct pushes, force-pushes, and
check-bypassing administrator merges to `main` are prohibited. Merging to `main` is a production
deployment event; the merge MUST NOT contain changes that were not present in the reviewed pull
request.

Rationale: `main` is the production branch. A pull request with green CI is the only repeatable
path that guarantees reviewed, tested code reaches production.

## Additional Constraints

### Supported Targets

Production releases MUST support Linux x64, Windows 10/11 x64, and Docker images for
`linux/amd64` plus `linux/arm64`. Dropping a supported target is a governance change and requires
a constitution amendment.

### Release Identity

Release tags MUST use the annotated form `vMAJOR.MINOR.PATCH`. The version MUST be synchronized
across CMake, the Tauri/Cargo/package metadata, Docker documentation, and the README. A release is
not complete until all tag-triggered workflows are green and the GitHub Release contains the
expected portable assets and `SHA256SUMS`.

### ONNX Runtime Pinning

The ONNX Runtime version used by CI, portable builds, and Docker MUST be pinned explicitly. A
version bump MUST update every build path in one change and MUST preserve the Windows
ONNX Runtime 1.17.1 compatibility test.

## Development Workflow & Quality Gates

Every pull request MUST run the repository CI and pass all required checks before merge. The
required checks include the Linux build with ONNX Runtime enabled, `ctest`, the CLI smoke test,
the Tauri `cargo check`, and a Docker `linux/amd64` build without publishing.

Every `v*` tag MUST run the Docker multi-architecture workflow and the portable-build workflow.
A release is green only when the Linux tar.gz, Linux AppImage, Windows zip, Windows NSIS
installer, `SHA256SUMS`, and multi-architecture Docker images are produced. A flaky Windows
runner failure MUST be rerun to green; it MUST NOT be waived.

A pull request that changes runtime behavior MUST add or update automated tests. A pull request
that changes the public API MUST update `docs/API.md` and `README.md` in the same change.

## Governance

This constitution supersedes conflicting local practices and informal conventions.

Amendments MUST be proposed through a pull request, include a Sync Impact Report, and update the
version and dates in this file. The version MUST follow semantic versioning:

- MAJOR for removing or redefining a principle in a backward-incompatible way.
- MINOR for adding a principle or section, or materially expanding guidance.
- PATCH for clarifications, wording fixes, and non-semantic refinements.

Every pull request review and every release MUST verify compliance with this constitution. A
violation blocks merge or release until it is fixed or the constitution is amended. No silent
exceptions are permitted; a temporary exception requires a documented amendment with an expiry
date.

**Version**: 2.0.0 | **Ratified**: 2026-11-13 | **Last Amended**: 2026-11-14
