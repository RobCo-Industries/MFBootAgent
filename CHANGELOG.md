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
- GPIO buttons on the library's example pins. UP and DOWN repeat while held. The `input` marker adds `gpio buttons repeat live`.
- A maintenance screen, opened by `M`, BACK, the port's `maint` or `boot_flags` bit 1, with Verify entries, System, Configuration and Reboot. New markers `maint_open`, `maint_action`, `verify_entry`, `verify_done` and `maint_close`.
- `boot_flags` bit 1 is set when the user opened the maintenance screen, and kept when it arrived set.
- `docs/USER_MANUAL.md`.

### Changed

- RobCo-Core is pinned at `796607f`.
- After a trusted handoff the stage adopts the screen with `fb_adopt()` and `console_adopt()`. It falls back to `fb_init()` and `console_init()`. The `screen` marker adds `path`, `adopt_fb` and `adopt_console`.
- The menu writes every one of its rows in full, and only its own rows.
- `BOOT.CFG` is read with `ini_parse_sections()` and follows the rules of BOOT_PROTOCOL.md: one result per entry header, names of letters, digits, `_` and `-`, 8.3 paths stored in upper case, printable `cmdline` only, last value wins. The `config` marker adds `nopath`.
- The subtype check runs before the ABI and CRC steps.

### Fixed

- Sections without a path no longer take menu slots. Eight of them followed by a valid ninth used to give the default menu.
- An image with a wrong subtype and a wrong CRC reported `crc`. It now reports `bounds`.
- Verify entries could not mount the card again after it was pulled out once. Verify entries and each boot now mount the card in the slot, and recover after any failure.
- A trusted console grid with too few rows for the menu put every menu row on the last row. A grid below 80 by 18 now gets a fresh screen, and the `screen` marker adds `grid`.

### Removed

- The pixel read-back of the earlier screen, `src/screen.c`, and its host tests.

- The prototype: its sources, payloads, tools, configuration files and documents.
