# Changelog

Notable changes in each release, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
(sections Added, Changed, Fixed, Removed; only the ones a release has), and versions follow
[Semantic Versioning](https://semver.org/). The release workflow copies a version's section into its GitHub release.

## [Unreleased]

### Changed

- The board tells the app which chip its firmware is built for; the app offers its firmware (ESP32-S3) only to ESP32-S3 boards, and installing it on another chip ends with a message that says so

### Fixed

- Firmware built for a single-core chip (ESP32-C3, C5, C6, H2) restarted over and over right after starting

## [0.7.4] - 2026-10-15

### Fixed

- Input devices that start the MTU exchange themselves (seen: AK87 SLIM PRO keyboard) were disconnected right after encryption and kept reconnecting
- A failure that repeats no longer stacks the same notice; notices have a close button and work while a dialog is open

## [0.7.3] - 2026-10-15

### Changed

- Default switch combos are Alt+1, 2, 3 (Option on a Mac keyboard), one hand on both Mac and PC keyboards. Combos you set yourself stay; "Restore default combos" in the app picks up the new ones

### Fixed

- Changing the switch combos sometimes failed with "That switch combo isn't valid": the board read a long command in two parts
- "Restore default combos" stayed visible after restoring
- Clicking another combo while recording one left both recording; a click anywhere else now ends recording

## [0.7.2] - 2026-10-15

First public release.

### Added

- One Bluetooth LE keyboard and mouse shared by up to three computers, switched by key combo without reconnecting
- Menu bar / tray app for macOS and Windows (Linux experimental) that installs and updates the board firmware
- Host names and reordering
- 7.5 ms connection interval for the active host, 15 ms fallback
- In-app updates from GitHub Releases
