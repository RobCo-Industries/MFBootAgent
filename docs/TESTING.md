# Testing

## Host tests

`make test-host` builds each `tests/host/test_<name>.c` with the pure files and runs it with AddressSanitizer and UBSan. It also compiles them with the cross GCC in two passes, at CI's flags with the sanitizers and at `-O2`, to catch the GCC warnings that CI reports. `make ci-local` runs a CI build in the robco-ci container with Docker. Both come from RobCo-Core's `mk/host.mk`.

Before the host tests, `make check-pins` runs `make check-workflows`. That target comes from RobCo-Core's `mk/robco.mk` and runs its `tools/check_workflow_exprs.py` on every file under `.github/workflows/`. It fails on a `${{ }}` inside a script and on a `uses:` not pinned by a full commit.

| File | Covers |
|---|---|
| `test_bootcfg.c` | `[maint]` skipped without a count. The spec's three examples verbatim, the order of results per entry header, every limit, the path and `cmdline` rules, upper-case paths, last value wins, `[boot]` twice, duplicates, more than 8 usable entries, unusable sections that take no slot, defaults, syntax errors |
| `test_menu.c` | Every key in every state, the countdown to zero, a key stopping it, `timeout = 0`, digits past the entries, the wrap of `timer_now_ms()` |
| `test_diag_menu.c` | The diagnostics menu and the live view exit rule. The port request rule of `src/portreq.c` for `diag`: live in its turn, stale once dropped, and no key for the menu. |
| `test_diag_fmt.c` | The diagnostics lines and markers, every row text within 26 columns, and the timer, RNG, video and expander verdicts |
| `test_diag_mem.c` | The memory ranges, the patterns, and the boot info's RAM against the mailbox's |
| `test_diag_sd.c` | The SD read plan, the image rule, the directory scan, the write guard and a blank card ID |
| `test_menulines.c` | Every line of the screen and the 79-column limit |
| `test_boot.c` | Loader results to codes and lines, the accepted subtypes, the header check order up to the subtype, the boot info fields this stage writes, the four cases of the `boot_flags` bit 1 rule |
| `test_keymap.c` | Every key code with press, repeat and release, every character, other events |
| `test_maint_ui.c` | The maintenance image's menu, the yes and no rule of a question, a held key that answers once, the stop keys, the tool names and every row within 79 columns |
| `test_maint_cfg.c` | `[maint]` of `BOOT.CFG`: off by default, `on` and `off` in any case, bad values, last value wins, two sections, unknown keys, a bad line |
| `test_maint_walk.c` | The check card walk over a fake tree: counts, each failure with its path, a folder read error, the depth and path limits, a stop that closes every folder, a root that does not open |
| `test_maint_home.c` | The `/UOS/HOME` entries view file lists and the `TERMINAL.CFG` path limit |
| `test_maint_view.c` | The view file pager: line ends, wraps, tabs, binary bytes shown as `.`, the 16384-byte cap, an empty file |
| `test_maint_clock.c` | The set clock form: buttons, typed digits, wraps, and the check of the chip's range and the calendar. With a fake clock: a failed write, a failed read back, a read back earlier or more than 2 s later |
| `test_maint_users.c` | The restore users decision table: only a yes after the question writes |
| `test_maint_marker.c` | Marker values with text from the card: each marker built with a hostile name, then split and decoded as a reader does. The encoder `dg_mval()` is shared by `MAINT.SYS` and `DIAG.SYS`. |
| `test_startkeys.c` | The start key rule on the road into `MAINT.SYS`: each button held through the handoff, its guard press, repeats and release dropped, the next press acting, port and console keys acting, another button acting |
| `test_maintreq.c` | The maintenance request: each row of the spec's failure table, the 63-byte line with each line end, binary and long content, the delete result, the screen at start with `why=request` or `flag`, the held BACK, every road for `boot_flags` bits 1 and 3 over subtypes, results, received flags and trust, and the `MAINT` entry or the fixed path |
| `test_maintscr.c` | The first BACK skipped after a request with BACK held, the request rows and the notice. Every key on every action, every page's lines, long names and paths cut as the menu cuts them, the screen on 80 by 18 and 80 by 30 grids |

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
| `test_maint_req.py` | `/ROBCO/MAINT.REQ` on cards made by `card()`: no file, a line, an empty file, binary content, 64 and 63 bytes, a folder of that name, a request with BACK held through the GPIO guard, a delete that fails, two boots in a row with the file gone from the card image after the first, a request then UOS with bits 1 and 3 read by the test OS, a request with bit 1 received from `tests/chain1maint`, bit 1 received with BACK held, through `after()` with qtest, `MAINT` from the menu with bit 1 set and bit 3 clear, the screen starting `/ROBCO/MAINT.SYS` with `choose index=-1`, with no `MAINT` entry and with no `BOOT.CFG`, and a missing `MAINT.SYS`. The test OS sealed with subtype 2 stands in for `MAINT.SYS`, because these tests read the flags from the image they boot and the test OS prints them. |
| `test_buttons.py` | GPIO buttons on the boot menu, BACK and DOWN and SELECT held at start, the all-lines-low guard, two buttons in the `DIAG.SYS` input view. See [GPIO buttons](#gpio-buttons). |
| `test_card_writes.py` | No QEMU: the agent links no writing call of the file system API but `fs_unlink()`, calls it once with the request's path, and opens every file with `FS_READ` |
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

Without `RETROS_IMG` these tests skip. The nightly run in RobCo-Core selects them by name, `-k whole_chain`.

- `test_chain.py`: `test_whole_chain` boots the test OS through the BIOS and the agent. `test_whole_chain_keeps_bios_text` checks the BIOS text above the menu.
- `test_chain_diag.py`: the agent's menu picks `DIAG`, the real `DIAG.SYS` boots, and `diag all` with the clock chip and both expanders ends with 7 OK, 1 ABSENT and 1 SKIPPED. The card sees no write.
- `test_chain_fallback.py`: `MFBOOT.SYS` broken by a flipped byte, a wrong magic or a cut file. With a good `MFBOOT.BAK`, the BIOS reports the error, falls back, sets `boot_flags` bit 0, and the agent clears the bit for the test OS. With both broken, the BIOS opens its emergency shell with `E08 CRC`, `E03 MAGIC` or `E05 SIZE`.

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


## The maintenance image

`tests/test_maint_*.py` start `build/maint.sys` alone, through a two-word image at `0x8000`, or from the agent's `MAINT` entry, with the helpers in `tests/maintimg.py`. They drive it through the maintenance port with `tool <name>`, `key`, `char` and `text`, and send Ctrl-C on the serial console. They read the screen only after `state()`. Each card test hashes the card file before and after a read-only tool, and reads `/UOS` with mtools after restore users.

- `test_maint_image.py`: the markers in order, the menu, the start from the agent's menu with a trusted handoff and an adopted screen, no card, the port commands, the `[maint]` cases of the spec's key table, a missing `BOOT.CFG` and a bad line. The shipped image answers `wprobe` with `err unknown command` and prints no `write_hook`. The test build prints it.
- `test_maint_card.py`: a good card, a file whose directory entry claims more bytes than its clusters hold, a card filled to its last 64 KiB with the port answering during the walk, a stop, and the write probe.
- `test_maint_images.py`: a good card, a truncated and a wrong-CRC image, an empty and a short one, no `/ROBCO`, and the write probe.
- `test_maint_view.py`: `BOOT.CFG` over three pages, a missing, an empty, an oversized and a binary file, ENTER to close, a cancel, and the write probe. `UOS.CFG` and `DEVICES.CFG`, two users of which one has a `TERMINAL.CFG`, 17 user folders with a cut list, and a `/UOS/HOME` with no `TERMINAL.CFG`.
- `test_maint_clock.py`: no chip, typed digits and a yes read back from QEMU's DS1338, buttons and ESC at the question, an invalid date and the year 2100, Ctrl-C.
- `test_maint_users.py`: the setting absent, off and bad; no default file and an empty one; `n`, ESC, Ctrl-C, `q` and DOWN at the question; ENTER, `y` and `Y`. A yes leaves `SYSUAF.DAT` equal to the default byte for byte and no work file. Every refusal leaves the old bytes.
- `test_maint_buttons.py`: GPIO buttons through `harness.pigpio`: UP, DOWN and SELECT on the menu, BACK at the view file chooser, BACK then SELECT at the restore users question. No button is pressed twice in a row, because nothing waits for the guest to see a release.
- `test_maint_restart.py`: restart from the port and from the menu, then the image boots again.
- `test_maint_road.py`: the real `MAINT.SYS` started from the agent. A request, the screen with `why=request`, its start action, and the image with a trusted handoff, an adopted screen, bits 1 and 3 in its `handoff` marker and `Started by a request.` on its status row. `MAINT` from the menu with bit 1 set and bit 3 clear and `Maintenance asked.`. The fixed path with no `MAINT` entry and with no `BOOT.CFG`. A restart from the image, then a normal boot with `maint_req result=none`. On raspi2b only, restore users through the chain: `[maint] restore_users = on`, a request, a yes, a restart, and `SYSUAF.DAT` equal to the default on the card image. SELECT held from power-on through the agent's screen into the image: the image's guard queues it, the menu drops it and its release, DOWN pressed after the release moves the bar, and the next SELECT runs that tool.

Without the start key rule in `maint/main.c`, `test_select_held_into_the_image_runs_no_tool` fails: check card runs at once. Without the flags on the status row, `test_request_to_the_real_image` and `test_maint_from_the_menu` fail.

## The failed delete

QEMU refuses a read-only drive as an SD card, and the agent has no fault hook. So `test_delete_fails_on_a_locked_file` sets the FAT read-only attribute on `MAINT.REQ` with `mattrib +r`. `fs_unlink()` then fails with `FS_ERR_DENIED`, -8. A failed `fs_stat()` with `rc=<code>` in the marker, a write-protected card and an I/O error during the delete are covered by the host tests of `src/maintreq.c` only: any non-zero code gives `clear_failed`.

Without `check_request()` in `main.c`, `test_line_shows_and_file_goes` fails. Without the subtype in the bit 1 rule, `test_maint_from_the_menu` fails.

## GPIO buttons

QEMU models no GPIO pulls, so an undriven line reads 0 and an active-low button reads pressed. RobCo-Core's `harness.pigpio` sets input levels through qtest. `boot(..., qtest=True, gpio_high=pins)` drives lines high before the guest starts, and `PiGpio` presses and releases them. `alone()` in `tests/mfboot.py` and in `tests/diag.py` passes both through.

`test_buttons.py` uses the pins of `src/maint.c` and `diag/main.c`: UP 5, DOWN 6, LEFT 17, RIGHT 22, SELECT 26, BACK 27.

| Test | Proves |
|---|---|
| `test_menu_up_down_select` | The buttons alone move the bar down, up and down, and SELECT boots the entry under it. The `input` marker reads `buttons=active` and `live=serial,gpio,script`. |
| `test_back_held_at_start_opens_maintenance` | BACK held from power-on, the other lines high: the guard passes, the held button comes as a press, and the screen opens with `why=button`. Nothing boots. |
| `test_down_held_at_start_stops_the_countdown` | DOWN held from power-on moves the bar to entry 2 and stops a 3 s countdown. Nothing boots after it. |
| `test_select_held_at_start_boots_the_default` | SELECT held from power-on boots the default entry by key, long before a 30 s countdown ends |
| `test_all_lines_low_disables_the_buttons` | No line driven: the backend reads `disabled_stuck`. Lines released and DOWN pressed later give no event. Serial keys still move the bar and boot. |
| `test_diag_input_view_shows_two_buttons` | In the `DIAG.SYS` input view, UP and SELECT held together give `gpio` press events and two PRESSED rows. Releasing both gives release events, no row stays pressed, and the view ends with `gpio=active`. |

A press is held until the guest reports its effect, then released. So no check depends on host timing, and none needs the library's timing rule in `tests/target/test_input_gpio.py`. A host stall only makes a test slower. UP and DOWN repeat after 500 ms. The menus in these tests have two entries, so a repeat at an end changes nothing. With SELECT or BACK moved to another pin in `src/maint.c`, `test_menu_up_down_select` and `test_back_held_at_start_opens_maintenance` fail.

### A button released before the start screen opens

The mount is fast in QEMU, so no host timing can release a button between the guard and the start screen. The test build `build/mfbtest/mfboot.img`, made by `make all` with `MFB_TEST=1`, holds the boot just before the start screen while LEFT is held from power-on. It prints `test_hold`. The tests in `test_maint_req.py` hold LEFT and BACK or SELECT, wait for `test_hold`, release the button, then LEFT. The button's release is then queued before the screen opens, and the screen must stay open with no action. LEFT does nothing on the screen. `test_shipped_image_has_no_hold` proves `build/mfboot.img` has no hold. No check depends on host timing.

What stays unverified until hardware:

- A QEMU press is a clean level change. It has no bounce, so the 20 ms debounce only delays it here. The library's own tests play bounce and chatter on its example image, not on this one.
- The backend samples on the tick. No edge interrupt is modelled or used, so an edge path cannot be checked.
- Pulls are not modelled. The internal pull-up that holds a released line high on a board is not exercised: the test drives the line high instead.
- Real switch wiring, contact resistance and a button that sticks after release.
- The delete of `MAINT.REQ` on a real card, and a card's write-protect switch.
