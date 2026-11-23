# Changelog

All notable changes to MFBootAgent are listed here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- A second stage built on RobCo-Core, as a sealed `BOOT` image of subtype 4 linked at `0x00200000`.
- Entry with the trust rule. Alone, the stage cold-boots. After a first stage, the screen continues below the earlier text.
- A self check of the image, reported and not enforced.
- The boot volume by `fs_mount_boot()`.
- `/ROBCO/BOOT.CFG` with `[boot] default` and `timeout`, and up to 8 `[entry NAME]` sections with `path` and `cmdline`.
- The boot menu on the screen and the serial console, with UP, DOWN, digits, ENTER, R and a countdown.
- Loading the chosen entry into the OS slot, the boot info update and the handoff.
- An error line under the menu when a load fails, and the halted state when no card or volume is usable.
- The maintenance port with `state`, `dump` and `reboot`.
- Host tests, QEMU tests on four boards, a test operating image and CI.

### Removed

- The prototype: its sources, payloads, tools, configuration files and documents.
