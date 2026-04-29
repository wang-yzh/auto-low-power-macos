# Changelog

All notable changes to this project will be documented in this file.

## [0.1.1] - 2026-04-30

### Changed

- Made release packaging deterministic via `git archive` and `.gitattributes`.
- Updated the Homebrew formula to point at the `v0.1.1` release asset.
- Kept the one-click installer on the latest tagged release path.

## [0.1.0] - 2026-04-30

### Added

- Event-driven Low Power Mode daemon for macOS laptops.
- Install and uninstall scripts for `launchd` deployment.
- Quick-install script for release-based bootstrap installation.
- Homebrew formula for tap-based installation.
- GitHub Actions CI workflow for build and script validation.
- Release packaging via `make package`.
- README with usage, troubleshooting, and operational notes.

### Changed

- Default threshold set to `25%`.
- Power-source detection uses `IOPowerSources` instead of relying on a single battery registry boolean.
- Duplicate correction attempts are rate-limited with a short cooldown to reduce repeated `pmset` writes and noisy logs.
