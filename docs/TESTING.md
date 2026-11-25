# Testing

## Host tests

`make test-host` builds each `tests/host/test_<name>.c` with the pure files and runs it with AddressSanitizer and UBSan. It also compiles them with the cross GCC to catch the GCC warnings that CI reports.

| File | Covers |
|---|---|
| `test_bootcfg.c` | The spec's three examples verbatim, the order of results per entry header, every limit, the path and `cmdline` rules, upper-case paths, last value wins, `[boot]` twice, duplicates, more than 8 usable entries, unusable sections that take no slot, defaults, syntax errors |
| `test_menu.c` | Every key in every state, the countdown to zero, a key stopping it, `timeout = 0`, digits past the entries, the wrap of `timer_now_ms()` |
| `test_menulines.c` | Every line of the screen and the 79-column limit |
| `test_boot.c` | Loader results to codes and lines, the accepted subtypes, the header check order up to the subtype, the boot info fields this stage writes |

## Target tests

The tests boot QEMU through RobCo-Core's harness on `raspi0`, `raspi1ap`, `raspi2b` and `raspi3b`. The maintenance port, the second serial port, injects keys and answers `state` and `dump`. Tests match complete serial lines only.

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

The test OS in `tests/os/` is built three times, as subtypes 0, 2 and 3. It checks the registers and the boot info it receives against the spec for a third stage and prints one `check` marker per check.

## The earlier text above the menu

After an adopt the console does not know the earlier cells, and `dump` shows them as `#`. So the tests check that text by its pixels:

- After chain1, the rows above the menu equal `chain1` and a blank row, drawn from the console font in amber. The tests take the font and the colours from RobCo-Core's `tests/target/test_console.py`.
- After RETROS-BIOS, a first run loads RobCo-Core's `examples/chain2` built with `CHAIN2_VIDEO_WAIT` as `MFBOOT.SYS`. It adopts the screen, writes nothing and waits, so its screenshot is the BIOS screen at the handoff. A second run loads MFBootAgent. The menu scrolls the screen, and the rows above it must equal the BIOS rows of the first screenshot, moved up by the scroll. The two BIOS rows that name the loaded image differ between the runs. They are compared with their text from the BIOS serial output, drawn from the font.
- `dump` checks the rest: every cell above the menu is unknown, and every cell of the menu is known.

## Goldens

`tests/golden/` holds screenshots: the menu, the error line and the screen at the handoff. They show no size, CRC or time. The handoff golden boots `tests/fixed_os.py`, a fixed image whose bytes are pinned. `ROBCO_UPDATE_GOLDEN=1` rewrites them.

## Timing

`test_default_boots_after_timeout` checks that the jump comes no sooner than the timeout, minus 0.1 s of output latency, and no later than the timeout plus `HOST_ALLOWANCE`, 3 s. Other QEMU instances on the same machine can slow the guest. When a timing test fails, run it alone before you conclude.

## The whole chain

Build a RETROS.IMG from RETROS-BIOS, then:

```sh
make test-target RETROS_IMG=/path/to/retros.img
```

## The diagnostics image

`tests/test_diag_*.py` start `build/diag.sys` alone, through a two-word image at `0x8000`, with the helpers in `tests/diag.py`. They drive it through the maintenance port and read its markers and screen.

- `test_diag_menu.py`: the markers, the menu golden, keys, port commands and reboot.
- `test_diag_mem.py`: the full range with every byte flushed from the D-cache before its check, a planted fault with the test build, and an abort by key.
- `test_diag_sd.py`: a good card, a corrupted image, a `DIAG.SYS` cut to 16 bytes, a damaged magic, an extra text file, a card without `/ROBCO`, a `/ROBCO` whose cluster chain is broken, a 64 MiB card read as a sample, and no card. The card file is unchanged after the good card test. With the test build, `sdwrite` sends one write through the guard: the test counts it, refuses it and fails, and the card file is unchanged. The next run sends no write and passes. `sdmulti` fails every multi-block read of one run: the run fails with `chunk_errors` above 0 and `errors=0`, and the next run passes.
- `test_diag_devices.py`: video patterns checked pixel by pixel, audio through the trace backend, the input live view and its three exits, timer, rng, and the clock chip present and absent.
- `test_diag_expander.py`: both expanders, one and none. The test presets outputs on the QEMU models, drives input lines with qtest, and checks that every pin the chips drive is the same at the end. It also checks that the image links no expander write function.
- `test_diag_all.py`: `diag all` with every device, on a bare board, and an abort that skips the rest and leaves the `RUN ALL` row at SKIPPED.

On a busy host, QEMU can stop the guest for longer than a tick period. The timer test then misses ticks and fails, as it must on a real board. The two `diag all` runs skip in that case and print the numbers, only when every other test gave its expected result and `window_missed` explains `us` within one period. Any other failure fails. `test_timer_starved_rule` checks the rule without QEMU, with numbers seen on raspi2b at a load average of 22.
- `test_diag_chain.py`: the agent's menu, choose `DIAG`, the image trusts the boot info and reaches `ready`. Alone and after `examples/chain1`.

QEMU models no cache. The memory test proves that the flush runs over every byte, not that it reaches RAM on a board. Nothing here is validated on hardware.

`test_diag_mem.py` allows a full pass `HOST_ALLOWANCE`, 20 s, above 10 s. The input idle test waits 20 s. Each test file takes under 35 s per board.
