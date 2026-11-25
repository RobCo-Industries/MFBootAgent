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
- `DIAG.SYS`, a diagnostics image of subtype 3 with tests of RAM, the SD card, video, audio, input, the timer, the RNG, the clock chip and the I/O expanders. `make sdimage` puts it on the card. A test reports OK only for what it measured: the memory test flushes the D-cache before each check, the SD test counts any write attempt and fails on a failed multi-block read even when every block reads alone, a failed expander attach fails, a PWM that fell back to the trace backend fails the audio test, a clean audio run on the trace backend reads ABSENT because no PWM was found, and a stopped run all is never OK. The timer test reports `window_missed`, the ticks missed inside its window. The video test fails on a pixel order or virtual offset that differs from the framebuffer in use. The timer test fails when the tick runs at a rate other than the one asked. The memory test fails when the boot info gives RAM past the mailbox's. The SD test fails on a CID of all zeros or all ones. The expander test fails when every register of a chip reads `0xff`. `docs/DIAG.md` lists what each test checks and what it only reports.
- GPIO buttons on the library's example pins. UP and DOWN repeat while held. The `input` marker adds `gpio buttons repeat live`.
- A maintenance screen, opened by `M`, BACK, the port's `maint` or `boot_flags` bit 1, with Verify entries, System, Configuration and Reboot. New markers `maint_open`, `maint_action`, `verify_entry`, `verify_done` and `maint_close`.
- `boot_flags` bit 1 is set when the user opened the maintenance screen, and kept when it arrived set.
- `docs/USER_MANUAL.md`.
- The maintenance request. After the mount the agent looks for `/ROBCO/MAINT.REQ`, reads its line, deletes it with `fs_unlink()` and prints `maint_req`. A request opens the maintenance screen with `why=request`, with the line and, after a failed delete, a notice. This delete is the only write the agent makes on the card.
- `boot_flags` bit 3 for a request found this boot. Bit 1 is also set for a request and for any maintenance image the agent starts.
- The maintenance screen's Start the maintenance image, from the `MAINT` entry or `/ROBCO/MAINT.SYS`.
- QEMU tests that press GPIO buttons through RobCo-Core's `harness.pigpio`: the boot menu with UP, DOWN and SELECT, BACK and DOWN held at start, the all-lines-low guard, and two buttons in the `DIAG.SYS` input view.

### Changed

- RobCo-Core moved to `c8220b2`.
- RobCo-Core is pinned at `25f36d8`. Its harness parks the spare cores of QEMU's raspi3b, and its host warning check compiles at CI's flags.
- The host timing skip limit is RobCo-Core's, per board. `tests/timing_skips.py` is gone. `--timing-skips-max` now counts per board.
- After a trusted handoff the stage adopts the screen with `fb_adopt()` and `console_adopt()`. It falls back to `fb_init()` and `console_init()`. The `screen` marker adds `path`, `adopt_fb` and `adopt_console`.
- The menu writes every one of its rows in full, and only its own rows.
- `BOOT.CFG` is read with `ini_parse_sections()` and follows the rules of BOOT_PROTOCOL.md: one result per entry header, names of letters, digits, `_` and `-`, 8.3 paths stored in upper case, printable `cmdline` only, last value wins. The `config` marker adds `nopath`.
- The subtype check runs before the ABI and CRC steps.
- `BOOT.CFG` skips `[maint]` without a count. Its keys counted as `unknown` before.
- The maintenance screen has no rule row under its title, to fit six actions in 18 rows.

### Fixed

- Sections without a path no longer take menu slots. Eight of them followed by a valid ninth used to give the default menu.
- An image with a wrong subtype and a wrong CRC reported `crc`. It now reports `bounds`.
- Verify entries could not mount the card again after it was pulled out once. Verify entries and each boot now mount the card in the slot, and recover after any failure.
- A trusted console grid with too few rows for the menu put every menu row on the last row. A grid below 80 by 18 now gets a fresh screen, and the `screen` marker adds `grid`.
- The port's `maint` sent after a key in one write could be answered `ok` and skipped, then open the screen after a failed load. The request now waits in the input queue behind earlier keys. A boot chosen first drops it with the new `maint_drop` marker.
- The diagnostics image's `diag` on the port no longer runs after a test that a key started ahead of it. The request now waits in the input queue behind earlier keys, as `maint` does. A key run or a reboot that comes first drops it with the new `diag_drop` marker.
- The start wait for the GPIO guard no longer drops an event. A button held at power-on now reaches the menu, so a held BACK opens the maintenance screen.

### Removed

- The pixel read-back of the earlier screen, `src/screen.c`, and its host tests.

- The prototype: its sources, payloads, tools, configuration files and documents.
