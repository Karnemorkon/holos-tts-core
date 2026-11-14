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
