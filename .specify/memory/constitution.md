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

Every release MUST keep all 27 preset voices available under their existing names, with their
existing behavior. The OpenAI-compatible `POST /v1/audio/speech` endpoint MUST remain usable by
clients that only replace `base_url`. It MUST accept `input`, `voice`, `response_format`, `speed`,
`stream`, and `model` (accepted and ignored) as documented, and MUST continue to support `wav` and
`pcm` responses. Voice cloning MAY add new voices, but it MUST NOT remove, rename, or shadow any of
the 27 presets. A change that breaks an existing client contract requires a project MAJOR release
and an explicit migration path.

Rationale: users and integrations depend on a stable voice catalog and request/response contract;
silent renames or removals break existing audio pipelines.

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

**Version**: 1.0.0 | **Ratified**: 2026-11-13 | **Last Amended**: 2026-11-13
