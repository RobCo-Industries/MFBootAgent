# Testing

## Host tests

`make test-host` builds each `tests/host/test_<name>.c` with the pure files and runs it with AddressSanitizer and UBSan. It also compiles them with the cross GCC to catch the GCC warnings that CI reports.

| File | Covers |
|---|---|
| `test_bootcfg.c` | The spec's three examples verbatim, the order of results per entry header, every limit, the path and `cmdline` rules, upper-case paths, last value wins, `[boot]` twice, duplicates, more than 8 usable entries, unusable sections that take no slot, defaults, syntax errors |
| `test_menu.c` | Every key in every state, the countdown to zero, a key stopping it, `timeout = 0`, digits past the entries, the wrap of `timer_now_ms()` |
| `test_menulines.c` | Every line of the screen and the 79-column limit |
| `test_boot.c` | Loader results to codes and lines, the accepted subtypes, the header check order up to the subtype, the boot info fields this stage writes, the four cases of the `boot_flags` bit 1 rule |
| `test_keymap.c` | Every key code with press, repeat and release, every character, other events |
| `test_maintscr.c` | Every key on every action, every page's lines, long names and paths cut as the menu cuts them, the screen on 80 by 18 and 80 by 30 grids |

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
| `test_maint.py` | The maintenance screen opened by `M`, BACK, the port and `boot_flags` bit 1 from `tests/chain1maint`. Each action, verify with good and bad entries and with the card ejected, the countdown not restarting, bit 1 in the next stage's boot info, the input back ends, the screen and verify goldens. |

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

## GPIO buttons

QEMU cannot set Pi GPIO input levels. Every line reads 0, so the active-low buttons look held and the library disables the back end. The host tests cover the key mapping. The target tests check the `input` marker, `buttons=disabled_stuck`, and that console and port keys work. A BACK the port injects stands in for the button. Button navigation is untested on hardware.
