# Testing

## Host tests

`make test-host` builds each `tests/host/test_<name>.c` with the pure files and runs it with AddressSanitizer and UBSan. It also compiles them with the cross GCC to catch the GCC warnings that CI reports.

| File | Covers |
|---|---|
| `test_bootcfg.c` | The spec's example verbatim, every limit, bad values, syntax errors, duplicates, more than 8 entries, a missing default |
| `test_menu.c` | Every key in every state, the countdown to zero, a key stopping it, `timeout = 0`, digits past the entries, the wrap of `timer_now_ms()` |
| `test_menulines.c` | Every line of the screen and the 79-column limit |
| `test_boot.c` | Loader results to codes and lines, the accepted subtypes, the boot info fields this stage writes |
| `test_screen.c` | Reading text and attributes back from pixels in every scheme and pixel order |

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
| `test_entry.py` | The first markers alone, with no card |
| `test_menu.py` | Markers in order, the menu golden, the countdown and its time bound, a key stopping it, UP, DOWN, digits, ENTER and serial arrows, `timeout = 0`, no timeout, the reboot key, the handoff golden |
| `test_config.py` | No `BOOT.CFG`, a syntax error, no usable entry, the counts and limits |
| `test_load.py` | Each load failure with its line, then another entry boots; the countdown does not restart; the error golden; no card; no volume |
| `test_chain.py` | After chain1: the old text stays, the boot info keeps earlier fields and sets new ones. With `RETROS_IMG`: BIOS, MFBootAgent and the test OS in order. |
| `test_env_hygiene.py` | No test copies the host environment |

The test OS in `tests/os/` is built three times, as subtypes 0, 2 and 3. It checks the registers and the boot info it receives against the spec for a third stage and prints one `check` marker per check.

## Goldens

`tests/golden/` holds screenshots: the menu, the error line and the screen at the handoff. They show no size, CRC or time. The handoff golden boots `tests/fixed_os.py`, a fixed image whose bytes are pinned. `ROBCO_UPDATE_GOLDEN=1` rewrites them.

## Timing

`test_default_boots_after_timeout` checks that the jump comes no sooner than the timeout, minus 0.1 s of output latency, and no later than the timeout plus `HOST_ALLOWANCE`, 3 s. Other QEMU instances on the same machine can slow the guest. When a timing test fails, run it alone before you conclude.

## The whole chain

Build a RETROS.IMG from RETROS-BIOS, then:

```sh
make test-target RETROS_IMG=/path/to/retros.img
```
