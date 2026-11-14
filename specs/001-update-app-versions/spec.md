# Feature Specification: update-app-versions

**Feature Branch**: `001-update-app-versions`

**Created**: 2026-11-14

**Status**: Draft

**Input**: User description: "потрібно також оновити версію застосунків для віндовс та лінукс щоб вони підтігли нові зміни"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Windows user updates desktop app to get new voices (Priority: P1)

A Windows user who currently has the previous HolosTTS desktop application installed wants to receive the new 29 CC0 voice catalog released in v0.8.9. They obtain the updated Windows application (installer or portable package), install/run it, and confirm the app reports the new version and includes the new voices.

**Why this priority**: Windows is a supported production target per the constitution; users on Windows cannot access the new voices until the application version reflects the new release.

**Independent Test**: Can be fully tested by installing the updated Windows package and verifying the displayed version and the presence of the 29 CC0 voices, without depending on other user stories.

**Acceptance Scenarios**:

1. **Given** a Windows machine with an older HolosTTS application installed, **When** the user installs the updated Windows package, **Then** the application reports version 0.8.9.
2. **Given** the updated Windows application, **When** the user checks the voice catalog, **Then** the 29 CC0 voices are available and the retired `Speaker_*` names are absent.

---

### User Story 2 - Linux user updates desktop app to get new voices (Priority: P1)

A Linux user who currently has the previous HolosTTS desktop application (AppImage / tar.gz portable) installed wants to receive the new 29 CC0 voice catalog released in v0.8.9. They obtain the updated Linux application, install/run it, and confirm the app reports the new version and includes the new voices.

**Why this priority**: Linux is a supported production target per the constitution; users on Linux cannot access the new voices until the application version reflects the new release.

**Independent Test**: Can be fully tested by installing the updated Linux package and verifying the displayed version and the presence of the 29 CC0 voices, without depending on other user stories.

**Acceptance Scenarios**:

1. **Given** a Linux machine with an older HolosTTS application installed, **When** the user installs the updated Linux package, **Then** the application reports version 0.8.9.
2. **Given** the updated Linux application, **When** the user checks the voice catalog, **Then** the 29 CC0 voices are available and the retired `Speaker_*` names are absent.

---

### Edge Cases

- What happens when a Windows or Linux user has the old version installed — are they prompted to update, or must they manually obtain the new package?
- How does the system handle a version mismatch between the core library (0.8.9) and the desktop application version?

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST update the Windows application version to reflect the new release (0.8.9).
- **FR-002**: System MUST update the Linux application version to reflect the new release (0.8.9).
- **FR-003**: System MUST keep the application version consistent across all Windows distribution artifacts (installer, portable package).
- **FR-004**: System MUST keep the application version consistent across all Linux distribution artifacts (AppImage, tar.gz portable).
- **FR-005**: Updated Windows and Linux applications MUST include the new 29 CC0 voice catalog and exclude the retired `Speaker_*` voices.
- **FR-006**: System MUST synchronize the application version with the core project version per the constitution, so all release artifacts carry the same version number.

### Key Entities *(include if feature involves data)*

- **Application version**: The release version displayed by and embedded in the Windows and Linux desktop applications; must match the core project version (0.8.9).
- **Distribution artifact**: The packaged form of the application for a given platform (Windows installer/portable, Linux AppImage/tar.gz) that carries the version and voice catalog.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The Windows application reports version 0.8.9 and users can complete the update to access the 29 CC0 voices.
- **SC-002**: The Linux application reports version 0.8.9 and users can complete the update to access the 29 CC0 voices.
- **SC-003**: All Windows and Linux application artifacts report the same version number (0.8.9), with no mismatched artifacts in circulation.
- **SC-004**: The retired `Speaker_*` voices are absent from the voice catalog in both updated Windows and Linux applications.

## Assumptions

- The target application version is 0.8.9, matching the core project release.
- Windows targets Windows 10/11 x64 (supported production target per the constitution).
- Linux targets desktop packages (portable archives/app bundles) in addition to existing container images.
- Version synchronization across all distribution artifacts is required, per the constitution's Release Identity principle.
- Users obtain updated packages manually (no auto-update mechanism is assumed for v1).
