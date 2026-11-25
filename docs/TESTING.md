# Testing

## Host tests

`make test-host` builds each `tests/host/test_<name>.c` with the pure files and runs it with AddressSanitizer and UBSan. It also compiles them with the cross GCC in two passes, at CI's flags with the sanitizers and at `-O2`, to catch the GCC warnings that CI reports. `make ci-local` runs a CI build in the robco-ci container with Docker. Both come from RobCo-Core's `mk/host.mk`.

| File | Covers |
|---|---|
| `test_bootcfg.c` | The spec's three examples verbatim, the order of results per entry header, every limit, the path and `cmdline` rules, upper-case paths, last value wins, `[boot]` twice, duplicates, more than 8 usable entries, unusable sections that take no slot, defaults, syntax errors |
| `test_menu.c` | Every key in every state, the countdown to zero, a key stopping it, `timeout = 0`, digits past the entries, the wrap of `timer_now_ms()` |
| `test_diag_menu.c` | The diagnostics menu and the live view exit rule. The port request rule of `src/portreq.c` for `diag`: live in its turn, stale once dropped, and no key for the menu. |
| `test_diag_fmt.c` | The diagnostics lines and markers, every row text within 26 columns, and the timer, RNG, video and expander verdicts |
| `test_diag_mem.c` | The memory ranges, the patterns, and the boot info's RAM against the mailbox's |
| `test_diag_sd.c` | The SD read plan, the image rule, the directory scan, the write guard and a blank card ID |
| `test_menulines.c` | Every line of the screen and the 79-column limit |
| `test_boot.c` | Loader results to codes and lines, the accepted subtypes, the header check order up to the subtype, the boot info fields this stage writes, the four cases of the `boot_flags` bit 1 rule |
| `test_keymap.c` | Every key code with press, repeat and release, every character, other events |
| `test_maintscr.c` | Every key on every action, every page's lines, long names and paths cut as the menu cuts them, the screen on 80 by 18 and 80 by 30 grids |

## Target tests

The tests boot QEMU through RobCo-Core's harness on `raspi0`, `raspi1ap`, `raspi2b` and `raspi3b`. The maintenance port, the second serial port, injects keys and answers `state` and `dump`. The port answers between input events, so a reply can come before the stage handled the keys sent ahead of it. The `state` line ends with `pending=<n>`, the input events not taken yet. The `state()` helper asks again until `pending=0`, so read the screen only after `state()`. Send several port lines in one write with `batch()`. It reads one reply per line, so no reply stays unread for a later `vm.script()` to take as its own. Tests match complete serial lines only.

How the stage starts:

| Mode | How |
|---|---|
| Alone | The firmware stub enters a two-word image at `0x8000` that jumps to `0x00200000`, where QEMU loaded `mfboot.img`. r3 holds no boot info, so the stage cold-boots. |
| After chain1 | RobCo-Core's `examples/chain1`, built with `CHAIN1_SD` and `CHAIN1_FB`, loads `/ROBCO/MFBOOT.SYS` from the card and hands over an amber console |
| After RETROS-BIOS | Only with `RETROS_IMG=<RETROS.IMG>`. Skipped with a reason otherwise. |

| File | Covers |
|---|---|
| `test_entry.py` | The first markers alone, with no card. A cold boot takes the `init` screen path and runs no adopt call. |
| `test_menu.py` | Markers in order, the menu golden, the countdown and its time bound, a key stopping it, UP, DOWN, digits, ENTER and serial arrows, `timeout = 0`, no timeout, the reboot key, the handoff golden |
| `test_config.py` | No `BOOT.CFG`, a syntax error, no usable entry, the counts and limits, eight sections without a path then a usable ninth with a lower-case path |
| `test_load.py` | Each load failure with its line, then another entry boots; a wrong subtype with a good CRC, a bad CRC or a bad ABI reports `bounds`; each operating subtype boots; the countdown does not restart; the error golden; no card; no volume |
| `test_chain.py` | After chain1: the screen is adopted, the old text stays, the boot info keeps earlier fields and sets new ones. After chain1 with no framebuffer: the `init` path. With `RETROS_IMG`: BIOS, MFBootAgent and the test OS in order, and the BIOS text above the menu. |
| `test_env_hygiene.py` | No test copies the host environment |
| `test_maint.py` | The maintenance screen opened by `M`, BACK, the port and `boot_flags` bit 1 from `tests/chain1maint`. Each action, verify with good and bad entries and with the card ejected, the countdown not restarting, `maint` and `key select` in one write in both orders, bit 1 in the next stage's boot info, the input back ends, the screen and verify goldens. |
| `test_port_replies.py` | `state()` and `batch()` against a fake port, no QEMU: a reply left unread by a raw write is reported |
| `test_timing_skips.py` | No QEMU: the library's host timing skip limit is loaded, and a starved timer skips with a reason it counts |

The test OS in `tests/os/` is built three times, as subtypes 0, 2 and 3. It checks the registers and the boot info it receives against the spec for a third stage and prints one `check` marker per check.

## The earlier text above the menu

After an adopt the console does not know the earlier cells, and `dump` shows them as `#`. So the tests check that text by its pixels:

- After chain1, the rows above the menu equal `chain1` and a blank row, drawn from the console font in amber. The tests take the font and the colours from RobCo-Core's `tests/target/test_console.py`.
- After RETROS-BIOS, a first run loads RobCo-Core's `examples/chain2` built with `CHAIN2_VIDEO_WAIT` as `MFBOOT.SYS`. It adopts the screen, writes nothing and waits, so its screenshot is the BIOS screen at the handoff. A second run loads MFBootAgent. The menu scrolls the screen, and the rows above it must equal the BIOS rows of the first screenshot, moved up by the scroll. The two BIOS rows that name the loaded image differ between the runs. They are compared with their text from the BIOS serial output, drawn from the font.
- `dump` checks the rest: every cell above the menu is unknown, and every cell of the menu is known.

## Goldens

`tests/golden/` holds screenshots: the menu, the error line, the screen at the handoff, the maintenance screen and the verify table. They show no size, CRC or time. The handoff golden boots `tests/fixed_os.py`, a fixed image whose bytes are pinned. `ROBCO_UPDATE_GOLDEN=1` rewrites them.

## Timing

`test_default_boots_after_timeout` checks that the jump comes no sooner than the timeout, minus 0.1 s of output latency, and no later than the timeout plus `HOST_ALLOWANCE`, 3 s. Other QEMU instances on the same machine can slow the guest. When a timing test fails, run it alone before you conclude.

## The whole chain

Build a RETROS.IMG from RETROS-BIOS, then:

```sh
make test-target RETROS_IMG=/path/to/retros.img
```

## The diagnostics image

`tests/test_diag_*.py` start `build/diag.sys` alone, through a two-word image at `0x8000`, with the helpers in `tests/diag.py`. They drive it through the maintenance port and read its markers and screen. The image's port shares the agent's `state` and `dump` protocol, so the tests use `state()` and `batch()` from `tests/mfboot.py`. Its `state` line also ends with `pending=<n>`, and the tests read the screen only after `state()`.

- `test_diag_menu.py`: the markers, the menu golden, keys, port commands and reboot. `diag` and `key select` in one write in both orders, and `diag` then `reboot`: the tests check which tests ran, their order, the `diag_drop` marker and that nothing runs later. QEMU hands the port's bytes over a few at a time, so the image could take SELECT before the `diag` line arrives. Each write starts with `text` and twelve characters the menu ignores. The menu takes one event per input poll, so SELECT is still queued when the port accepts `diag`.
- `test_diag_mem.py`: the full range with every byte flushed from the D-cache before its check, a planted fault with the test build, and an abort by key.
- `test_diag_sd.py`: a good card, a volume that ends past the card, a corrupted image, a `DIAG.SYS` cut to 16 bytes, a damaged magic, an extra text file, a card without `/ROBCO`, a `/ROBCO` whose cluster chain is broken, a 64 MiB card read as a sample, and no card. The card file is unchanged after the good card test. With the test build, `sdwrite` sends one write through the guard: the test counts it, refuses it and fails, and the card file is unchanged. The next run sends no write and passes. `sdmulti` fails every multi-block read of one run: the run fails with `chunk_errors` above 0 and `errors=0`, and the next run passes.
- `test_diag_devices.py`: video patterns checked pixel by pixel, the pixel order, offset, virtual size and alpha keys, audio through the trace backend that reads ABSENT, the input live view and its three exits, timer, rng, and the clock chip present and absent.
- `test_diag_expander.py`: both expanders, one and none. The test presets outputs on the QEMU models, drives input lines with qtest, and checks that every pin the chips drive is the same at the end. It also checks that the image links no expander write function.
- `test_diag_all.py`: `diag all` with every device, where audio reads ABSENT and the run stays OK, on a bare board, and an abort that skips the rest and leaves the `RUN ALL` row at SKIPPED.

On a busy host, QEMU can stop the guest for longer than a tick period. The timer test then misses ticks and fails, as it must on a real board. The two `diag all` runs and `test_timer_and_rng` skip in that case and print the numbers, only when the timer is the only failure, `missed` is above 0 and `window_missed` explains `us` within one period. Any other failure fails. `test_timer_and_rng` runs rng first, so a skip never hides it. `test_timer_starved_rule` checks the rule without QEMU, with numbers seen on raspi2b at a load average of 22.

These skips go through RobCo-Core's `harness.hosttiming.skip()`, with the reason prefix `host timing:`. `tests/conftest.py` loads the library's `tests/target/conftest.py`, which counts them per board and fails the session when one board has more than one. On a loaded local machine, `--timing-skips-max=<n>` or `make test-target TIMING_SKIPS_MAX=<n>` allows more per board. `make test-target` runs pytest with `-rs`, so every skip reason prints at the end. The library tests its counting. `test_timing_skips.py` checks that this project loads it. The rule that decides when a timer failure may skip stays here in `tests/diag.py`: the library's skip carries no evidence check.
- `test_diag_chain.py`: the agent's menu, choose `DIAG`, the image trusts the boot info and reaches `ready`. Alone and after `examples/chain1`. With both features: the maintenance screen's Verify entries passes the real `DIAG.SYS`, then `DIAG` boots from the menu with `boot_flags` bit 1 set and shows its menu.

QEMU models no cache. The memory test proves that the flush runs over every byte, not that it reaches RAM on a board. Nothing here is validated on hardware.

`test_diag_mem.py` allows a full pass `HOST_ALLOWANCE`, 20 s, above 10 s. The input idle test waits 20 s. Each test file takes under 35 s per board.

## GPIO buttons

QEMU cannot set Pi GPIO input levels. Every line reads 0, so the active-low buttons look held and the library disables the back end. The host tests cover the key mapping. The target tests check the `input` marker, `buttons=disabled_stuck`, and that console and port keys work. A BACK the port injects stands in for the button. Button navigation is untested on hardware.
