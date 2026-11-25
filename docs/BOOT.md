# Boot flow

MFBootAgent implements the second stage of [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/c8220b2731b90d0446db636a4dc986a3ce1035cb/docs/spec/BOOT_PROTOCOL.md) in RobCo-Core. That document is the contract. This page says what this stage does and which choices it made where the contract is open.

Every marker line has the form `@RC stage=bootagent ev=<event> ...` on the PL011.

## Steps

### 1. Entry

1. The self check runs first, before any code writes initialised data: `rimg_self_check()` over the image as loaded.
2. The PL011 starts and the `boot` marker prints.
3. The trust rule runs in `robco_start()`. `handoff trusted=0|1` gives the result.
4. Cold, the stage copies the firmware's ATAGS or DTB to `0x00101000`, as a first stage does.
5. The screen. See [Screen](#screen). `screen` reports the path it took.
6. The tick, the GPIO buttons, the console input and the maintenance port start. The stage waits up to 300 ms for the buttons' stuck-line guard. The wait takes no event: the presses the guard reports for held buttons stay queued for the menu. `input` reports each back end and the live ones.
7. `selfcheck` prints the result of step 1. A failed self check is information only: the boot goes on.

### Screen

The stage keeps the screen of the previous stage when it can. It follows the "Adopting the screen" pattern of RobCo-Core.

| Case | Calls | `path` | `kept` |
|---|---|---|---|
| Trusted boot info with caps bit 0 set | `bootinfo_fb_info()`, `fb_adopt()`, then `console_adopt()` with the boot info's grid, cursor and scheme | `adopt` | 1 |
| Trusted, but `bootinfo_fb_info()`, `fb_adopt()` or `console_adopt()` fails | `fb_init(640, 480, 32)` and `console_init()` with the boot info's scheme | `init` | 0 |
| Trusted, both adopt calls succeed, but the grid is below the minimum | `fb_init(640, 480, 32)` and `console_init()` with the boot info's scheme | `init` | 0 |
| Trusted with caps bit 0 clear | `fb_init(640, 480, 32)` and `console_init()` with the boot info's scheme | `init` | 0 |
| Cold | `fb_init(640, 480, 32)` and `console_init()` in green | `init` | 0 |

The menu needs a grid of at least 80 columns and 18 rows. The longest menu line has 79 characters, and the last column stays blank so no line wraps. The menu with 8 entries takes 18 rows. The stage picks its screen before it reads `BOOT.CFG`, so it checks for 8 entries. `console_adopt()` accepts any grid that fits the framebuffer, so the stage checks the adopted grid itself. A smaller grid gets a fresh screen, and the `grid` key of `screen` says why. The fresh 640 by 480 screen gives 80 by 30.

The adopt path writes no pixel. The earlier text stays as it is, and the cursor stays where the previous stage left it. The console does not know the characters of those cells: its `dump` shows them as `#`.

So the menu only writes its own rows:

- Every menu row is written once across the whole width of the grid, the blank rows too. No earlier text shows inside the menu.
- A redraw writes only the rows that changed: an entry, the countdown line, the error line or the `Starting` line.
- When the menu does not fit below the cursor, the console scrolls up. Scrolling moves pixels, so the earlier text moves up intact.
- Nothing ever repaints the whole screen or changes the scheme.

### 2. Boot volume

The stage calls `sd_init()`, then `fs_mount_boot()` with two markers: `/ROBCO/BOOT.CFG` and `/ROBCO/UOS.SYS`, the default OS path. The first volume that holds either file is the boot volume. The stage does not use `boot_part_lba` from the boot info to choose: every stage applies the rule itself.

The volume is mounted with write support. The stage writes exactly one thing on the card: the delete of `/ROBCO/MAINT.REQ`, see [Maintenance request](#maintenance-request). Every other file opens for reading only. `tests/test_card_writes.py` checks that no other writing call of the file system API is linked in.

### Maintenance request

Right after the mount, before `BOOT.CFG`, the menu and the countdown, the stage looks for `/ROBCO/MAINT.REQ` by its plain name. It runs no recovery of work names. `src/maintreq.c` holds the rules, `main.c` runs the calls.

1. `fs_stat()`. A missing name, a folder or a failed call is no request. A folder stays as it is.
2. `fs_read_file()` into a 65-byte buffer. The line shows on the screen when it has 1 to 63 bytes from 0x20 to 0x7E, with one optional CR, LF or CRLF at the end. An empty file, a longer content, a failed read or any other byte shows no line. The request counts either way.
3. `fs_unlink()`. The delete comes before any action, so the request is one-shot. `fs_unlink()` syncs the volume before it returns.
4. `maint_req result=found rc=0`, or `result=clear_failed rc=<code>` when the delete failed. With no request, `result=none rc=0`. The marker prints once per boot in which the volume mounted. When nothing mounts, the stage halts and prints no `maint_req`.
5. The menu shows with no countdown, then the maintenance screen opens with `maint_open why=request`. Its output rows show `MAINTENANCE REQUESTED` and the request line, if any. After a failed delete they add a notice: the file could not be deleted, with the code, and the request comes back at each start until the file is removed.

The stage never loops. Closing the screen leaves the menu with no countdown, and no image boots without a key. A delete that keeps failing opens the screen again at each start.

A request with `boot_flags` bit 1 received opens one screen, `why=request`. A request with BACK held through the button guard opens one screen too. The guard queues a press for the held BACK, and the screen ignores that first BACK so it does not close at once. The next BACK closes it as usual.

### 3. BOOT.CFG

The stage reads `/ROBCO/BOOT.CFG`, at most 4096 bytes. See [BOOT.CFG](#bootcfg).

### 4. Menu

The menu shows below the text already on the screen, with a blank row between. When it does not fit, the screen scrolls up. It shows the same lines on the PL011. After the first draw, only changed lines are drawn again, on both.

| Key | Action |
|---|---|
| UP, DOWN | Move the selection. It stops at the first and last entry. |
| 1 to 8 | Select that entry, if it exists |
| ENTER | Boot the selection |
| R | Reboot through the watchdog |
| M, or BACK from the buttons or the port | Open the [maintenance screen](#maintenance-screen) |
| Any key | Stops the countdown |

Keys come from the PL011, as ANSI sequences, from the GPIO buttons and from the maintenance port's `key` and `char` commands. `src/keymap.c` maps every event to one key. Only UP and DOWN repeat while held. LEFT and RIGHT do nothing. ESC on the PL011 is BACK from the serial source, and in the menu it only stops the countdown.

### GPIO buttons

The buttons use the example pins of BOARD_SUPPORT.md in RobCo-Core: UP 5, DOWN 6, LEFT 17, RIGHT 22, SELECT 26, BACK 27. Each is active low with the internal pull-up, debounced over 20 ms. Held keys repeat after 500 ms, then every 150 ms. If every line reads pressed for the 100 ms guard, the library disables the back end and the stage goes on with the others. A button held through the guard, with the other lines released, comes as a press when the guard ends. The menu takes it like any key: a held BACK opens the maintenance screen, and any held key stops the countdown. QEMU reads an undriven line as 0, so with no driven line the back end is `disabled_stuck`. The target tests drive the lines through qtest, see [TESTING.md](TESTING.md#gpio-buttons). Button navigation is untested on hardware.

### Maintenance screen

It opens on `M`, on BACK from the buttons or the port, on the port's `maint` command, at start after a [maintenance request](#maintenance-request), and at start when the boot info is trusted and has `boot_flags` bit 1 set. At start it takes the place of the countdown: the menu shows with no countdown, then the screen opens. Opening it any other way stops the countdown like a key. Closing it draws the menu again below it. The countdown does not start again.

The port's `maint` goes through the input queue behind the keys sent before it, so the menu takes them in the order the port got them. `maint` then `key select` opens the screen, and SELECT acts inside it. `key select` then `maint` boots: the stage drops the request with `maint_drop why=boot` before the load, and a failed load does not open the screen later. When a key opens the screen before the request comes up, the stage drops the request with `maint_drop why=open`. A full queue answers `err busy`.

The screen takes 18 rows below the text on screen, scrolling like the menu, and writes each of its rows in full. Rows: title, six actions, the keys, then ten output rows. UP and DOWN move the bar, ENTER or SELECT runs the action, ESC, Q or BACK closes it.

| Action | Does |
|---|---|
| Verify entries | Mounts the volume afresh, as in [Card changes](#card-changes), then runs the header read and `loader_load()` of a boot on every entry, without the jump. The OS slot is the work buffer, since nothing runs there yet. One row per entry, then the counts. A failed mount gives one `E02 IO` line. |
| System | Board, memory, boot volume, trust, `boot_flags` received, version, RobCo-Core pin, screen path |
| Configuration | The `config` result, default, timeout, entry count, and the `ignored`, `dup`, `bad` and `clamped` counts |
| Start the maintenance image | Closes the screen and boots the maintenance image as an entry chosen by key: the path of the `MAINT` entry when `BOOT.CFG` has one, else `/ROBCO/MAINT.SYS`. The fallback menu has no `MAINT` entry, so it uses the fixed path. `choose` gives the entry's index, or `index=0` for the fixed path. A missing or bad file is the usual load error under the menu. |
| Reboot | `reboot source=screen`, then the watchdog, as the port's `reboot` |
| Back to the boot menu | Closes the screen |

The RobCo-Core pin comes from the CI workflow pin, which `make check-pins` holds equal to the submodule.

The countdown boots the default entry no sooner than `timeout` seconds after the menu shows, by `timer_now_ms()`. The menu loop waits for input at most until the next second of the countdown, so the boot starts on the first pass after the time is up. The countdown uses the unsigned difference of two `timer_now_ms()` values, so it also works across the wrap of that counter after 2^32 ms.

### 5. Load and handoff

Before step 1 the stage mounts the volume afresh, as in [Card changes](#card-changes). A failure there gives `E02 IO` and `ev=remount what=<step> rc=<code>`, and the menu stays.

1. The stage reads the first 0x60 bytes of the file and checks the header, in this order:

   | Step | Check | Code |
   |---|---|---|
   | 1 | Magic `BOOT` | `magic` |
   | 2 | Header version and size | `version` |
   | 3 | `file_size` matches the FAT size | `size` |
   | 4 | Subtype 0 UOS, 2 maintenance or 3 diagnostics | `bounds`, "not an operating image" |
   | 4 | `load_addr` and `mem_size` inside the OS slot | `bounds` |
   | 5 | ABI major 2 | `abi` |

   `loader_load()` has no hook between its header steps, so the stage reads the header itself before it calls the loader. An image with a wrong subtype fails with `bounds` even when its ABI or CRC is also wrong.
2. `loader_load()` reads the file into the OS slot, `0x00400000` to `0x00FFFFFF`, 12 MiB. It runs the header steps again, then step 6, the CRC (`crc`).
3. The stage unmounts the volume, writes `Starting <name>.` and moves the cursor to the next row.
4. `bootinfo_build()` fills the block at `0x00100000` with stage id 2. The stage sets or clears `boot_flags` bits 1 and 3 by the rule below, whatever it received, with the library's `ROBCO_BOOT_MAINT` and `ROBCO_BOOT_MAINT_REQ`. Then it writes `boot_dev` (1, SD on EMMC), `boot_part_lba`, `boot_path` and `cmdline`, clears the fallback flag and reseals the block. Other flags stay as the previous stage set them.
5. `robco_handoff()` enters the image with r2 set to the firmware blob copy.

| Bit | Set when, this boot | Else |
|---|---|---|
| 1 | A request was found, the maintenance screen opened, by any road or by a trusted bit 1 received, or the image is a maintenance image, subtype 2, from the menu, the screen or a key the port sent | Clear |
| 3 | A request was found, `found` or `clear_failed`, whatever entry boots | Clear |

So `MAINT.SYS` always gets bit 1. UOS after a request gets bits 1 and 3: they say what was asked, not what started.

### Card changes

The SD slot has no card detect, so the stage cannot see a card change. Verify entries and each boot from the menu call `mfb_remount()`:

1. `fs_unmount("sd")`. It syncs the device first and keeps the volume when the sync fails. With the card out, or after a failed `sd_init()`, the sync returns `BLK_ERR_NOMEDIA`.
2. On a failure, `sd_init()` and then `fs_unmount("sd")` again. A card that answers lets the sync pass. When `sd_init()` fails, the step is `sd`, with its code.
3. `sd_init()` and `fs_mount_boot()` by the boot rule.

After any failure, `sd_deinit()` leaves the driver not ready. A volume still mounted then fails every read with `BLK_ERR_NOMEDIA`, so it never reads another card. The next call starts from step 1 and recovers once a card answers. RobCo-Core has no call that drops a volume without the sync. Dropping it loses nothing: the one write, the delete of the request, syncs before it returns, and every other file opens for reading only.

`BOOT.CFG` is read once, at start. A card change keeps the menu entries and the Configuration page from the start. Verify entries checks those entries against the card in the slot, and a boot loads the entry's path from it. `boot_part_lba` comes from the mount the image loaded from. The System page shows the volume mounted now with its first block and size. When the last mount failed, it shows `not mounted` with the failed step and its code, then a `Last good` line: the last volume that mounted, or `none since power-on`. Re-reading `BOOT.CFG` would change the menu under the user, so a new card's entries need a reboot.

### 6. Failures

| Failure | What happens |
|---|---|
| The chosen entry fails to load | `ev=error` once per attempt. The error line shows under the menu, which stays. The countdown does not start again. |
| `BOOT.CFG` missing, bad or with no usable entry | The menu shows one entry, `UOS` at `/ROBCO/UOS.SYS`, with no timeout |
| No card, or no volume mounts | `ev=error code=io`, the error and `BOOT HALTED` on screen, `ev=idle`. The stage waits and the maintenance port answers. It never reboots by itself. |

## Markers

In the order they print.

| Event | Keys | When |
|---|---|---|
| `boot` | `midr soc base board_rev mem entry_mode version` | First line |
| `handoff` | `trusted` | Result of the trust rule |
| `screen` | `fb console kept unknown scheme path adopt_fb adopt_console grid` | `kept=1` when both adopt calls succeeded. `unknown` is the number of adopted cells the console does not know, 0 on the `init` path. `path` is `adopt` or `init`. `adopt_fb` and `adopt_console` are the return codes of the adopt calls, or `none` when a call did not run. `grid` checks the adopted grid against the menu's minimum: `ok`, `cols` for too few columns, `rows` for too few rows, or `none` when `console_adopt()` did not succeed. |
| `input` | `serial mini script gpio buttons repeat live` | Return codes of the input back ends and of `input_set_repeat()`. `buttons` is the GPIO status name, such as `active` or `disabled_stuck`. `live` lists the back ends that deliver events, such as `serial,script`. |
| `selfcheck` | `rc size` | Result of `rimg_self_check()` |
| `mount` | `lba blocks` | The boot volume mounted |
| `maint_req` | `result rc` | The [maintenance request](#maintenance-request) check: `none`, `found` or `clear_failed`, and the delete's code |
| `config` | `result path`, then `entries ignored dup unknown bad clamped default_found nopath`, or `why` | `result` is `ok`, `missing` or `bad`. With `bad`, `why` is `read` with `rc`, `syntax` with `line`, or `empty` with the counters except `default_found`. See [Counters](#counters). |
| `entries` | `count default timeout` | The menu in use. `timeout` is `none` when the menu waits. |
| `countdown` | `state`, `seconds` | `state=start` when the menu shows with a timeout above 0, `state=stopped` when a key stops it |
| `ready` | | The menu shows |
| `select` | `index name` | The selection moved. `index` counts from 1. |
| `choose` | `index name path via` | An entry is to boot. `via` is `key` or `timeout`. The screen's Start the maintenance image gives `via=key`, and `index=0` with no `MAINT` entry. |
| `error` | `code path name`, or `code path what rc` | A load failed, or `code=io` with `what=sd` or `what=fat` |
| `load` | `path size crc addr name subtype` | The image loaded and passed every check |
| `bootinfo` | `flags caps dev part_lba path cmdline_len scheme cx cy` | The boot info as handed over |
| `jump` | `entry name` | Last line before the handoff |
| `idle` | | The stage stopped and waits |
| `reboot` | `source` | `key`, `maint` or `screen` |
| `maint_open` | `why` | The maintenance screen opened: `key`, `button`, `port`, `flag` or `request` |
| `maint_drop` | `why` | The port's `maint` was answered `ok` but did not open the screen: `boot` when a boot was chosen first, `open` when the screen was already open |
| `maint_action` | `name` | An action ran: `verify`, `system`, `config`, `start`, `reboot` or `back` |
| `verify_entry` | `index name path result` | One entry checked. `result` is `ok` or the `error` code. |
| `verify_done` | `result entries good failed`, with `what rc` for `result=io` | The verify action ended. `result` is `ok` or `io`. |
| `maint_close` | | The screen closed |

## Error codes

The codes are the loader's, from [IMAGE_FORMAT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/c8220b2731b90d0446db636a4dc986a3ce1035cb/docs/spec/IMAGE_FORMAT.md). The screen line is `ERROR E<code> <NAME>: <entry>: <reason>`.

| Code | `code=` | Reason on screen |
|---|---|---|
| E01 | `nofile` | file not found |
| E02 | `io` | SD card or FAT volume not usable |
| E03 | `magic` | not a boot image |
| E04 | `version` | unknown header version |
| E05 | `size` | size does not match the file |
| E06 | `bounds` | image does not fit the OS slot |
| E06 | `bounds` | not an operating image |
| E07 | `abi` | wrong ABI version |
| E08 | `crc` | image CRC does not match |

## BOOT.CFG

INI text, read with RobCo-Core's `ini_parse_sections()`. The stage parses the file once to check it, and acts on its sections and keys only when that parse returns 0.

```
[boot]
default = UOS
timeout = 3

[entry UOS]
path = /ROBCO/UOS.SYS
cmdline =
```

| Section | Key | Value | Default |
|---|---|---|---|
| `[boot]` | `default` | An entry name | The first entry |
| `[boot]` | `timeout` | Seconds, 0 to 99 | None: the menu waits for a key |
| `[entry NAME]` | `path` | Absolute 8.3 path, see the rule below | None: the entry is dropped |
| `[entry NAME]` | `cmdline` | Up to 127 printable ASCII characters | Empty |

| Limit | Value |
|---|---|
| Usable entries | 8. A dropped section takes no slot. |
| Entry name | 1 to 15 characters, each a letter, a digit, `_` or `-` |
| `path` | 63 characters, at most 8 components |
| Path component | 1 to 8 characters, then optionally a dot and 1 to 3 characters. Each a letter, a digit, `_`, `-` or `~`. |
| `cmdline` | 127 characters, each 0x20 to 0x7E after INI decoding |
| `timeout` | 0 to 99. A larger number becomes 99. |
| File | 4096 bytes |

### Entry sections

An entry header is `entry`, one or more spaces or tabs, then the name. The stage takes each header in file order and gives it exactly one result. The tests run in this order:

| Order | Test | Result |
|---|---|---|
| 1 | `[entry]` with no name, or a name that breaks the name rule | Dropped, `bad` |
| 2 | A name an earlier entry header used, in any case, even a dropped or ignored one | Dropped, `dup` |
| 3 | No `path` key | Dropped, `bad` and `nopath` |
| 3 | A `path` that breaks the path rule, or a `cmdline` with a byte outside 0x20 to 0x7E | Dropped, `bad` |
| 4 | The 8 slots are full | `ignored` |
| 5 | Otherwise | Takes the next slot |

- A dropped section never takes a slot. Eight sections without a path, then a ninth with one: the menu shows the ninth.
- The first header with a name always keeps it. The menu shows the name as written there.
- Within one section, a key given twice takes its last value. A second, bad `path` drops the entry.
- The keys of a dropped or ignored section count nowhere else.
- The path is stored and compared in upper case. `boot_path` and the `choose` marker hold that form. `path = /robco/uos.sys` loads `/ROBCO/UOS.SYS`.
- An empty component, a `/` at the end, `.` and `..` break the path rule.
- A `\n` escape in a quoted `cmdline`, a tab or a byte above 0x7E drops the whole entry. The stage never boots an entry with a changed command line. `ini.h` itself rejects a raw control byte other than a tab, and 0x7F, so such a file is bad.

### The boot section

- `[boot]` given twice acts as one section. A key given twice takes its last value.
- `timeout` follows the rule of `delay_ms` in BOOT_PROTOCOL.md: one or more ASCII digits and nothing else. A value above 99 becomes 99 and counts as `clamped`. A bad value counts as `bad` and the menu waits.
- `timeout = 0` shows the menu, then boots the default at once. No `timeout` key: the menu waits.
- `default` names an entry, in any case. A name that is in no slot, a dropped or ignored section included, or no `default` key, means the first slot.

### Other sections and whole-file cases

- Unknown keys, keys before the first section and keys in unknown sections count as `unknown`. `[bios]` belongs to RETROS-BIOS and `[maint]` to the maintenance image. Both are skipped without a count.
- A line that does not parse makes the whole file bad, `why=syntax`. A `cmdline` over 127 characters is past the INI reader's value limit, so it does the same.
- A file that parses but fills no slot is bad too, `why=empty`. An empty file is one of them.
- A missing file, a read error or a bad file gives the menu with one entry, `UOS` at `/ROBCO/UOS.SYS`, and no timeout.

### Counters

| Key | Counts |
|---|---|
| `entries` | Filled slots |
| `ignored` | Usable entries past the eighth |
| `dup` | Entry headers whose name an earlier entry header used |
| `bad` | Entry headers with a bad or missing name, a missing or bad `path`, or a bad `cmdline`. Also a bad `timeout`. |
| `nopath` | The part of `bad` that had no `path` key |
| `unknown` | Unknown keys, keys before any section, keys in unknown sections |
| `clamped` | A `timeout` above 99 |
| `default_found` | 1 when `default` names an entry in a slot |

## Spec gaps and choices

| Gap | Choice |
|---|---|
| Markers for this stage besides `boot`, `handoff`, `ready`, `load`, `error`, `jump` | `screen`, `input`, `selfcheck`, `mount`, `config`, `entries`, `countdown`, `select`, `choose`, `bootinfo`, `idle`, `reboot`, in the format of the spec |
| `config` counters | The spec's keys, plus `nopath` |
| `screen` keys | The spec's keys, plus `path`, `adopt_fb`, `adopt_console` and `grid` |
| Timeout rule | Seconds 0 to 99, digits only, no key means wait |
| A file that parses with no usable entry | Treated like a bad file: the one default entry |
| A boot agent or other non-OS subtype | Refused with code 6, `bounds`, before the ABI and CRC steps |
| Boot volume markers | `/ROBCO/BOOT.CFG` and `/ROBCO/UOS.SYS` |
| `boot_flags` after this stage | Fallback bit cleared. Bits 1 and 3 by the spec's rule, see [5. Load and handoff](#5-load-and-handoff). Other bits kept. |
| `fs_stat()` of `MAINT.REQ` fails with an error other than a missing name | Treated as no request: `maint_req result=none rc=0`. The spec does not name this case. |
| `choose` for the fixed path `/ROBCO/MAINT.SYS` | `index=0 name=MAINT`, since no menu entry holds it. The spec does not name the index. |
| A request with BACK held at start | The screen ignores the BACK press the guard queued, so it stays open |
| `MAINT` entry name | Matched in any case, as `BOOT.CFG` compares entry names |
| Markers of the maintenance screen | `maint_open`, `maint_drop`, `maint_action`, `verify_entry`, `verify_done`, `maint_close`, and `source=screen` for `reboot` |
| `input` keys | The spec's keys, plus `gpio buttons repeat live` |
| Which BACK opens the screen | BACK from the buttons or the port. ESC on the PL011 only stops the countdown, so ESC can close the screen without opening it again. |
| `maint` on the port outside the menu | Refused with `err not in the menu`, or `err already open` while the screen shows or a request waits. Queued behind earlier keys, see [Maintenance screen](#maintenance-screen). |
| The screen after a handoff | Adopted, see [Screen](#screen) |
