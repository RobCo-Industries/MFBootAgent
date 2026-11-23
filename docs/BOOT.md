# Boot flow

MFBootAgent implements the second stage of [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/c89d53ab9d600b530d18a49e0e8be570726ed825/docs/spec/BOOT_PROTOCOL.md) in RobCo-Core. That document is the contract. This page says what this stage does and which choices it made where the contract is open.

Every marker line has the form `@RC stage=bootagent ev=<event> ...` on the PL011.

## Steps

### 1. Entry

1. The self check runs first, before any code writes initialised data: `rimg_self_check()` over the image as loaded.
2. The PL011 starts and the `boot` marker prints.
3. The trust rule runs in `robco_start()`. `handoff trusted=0|1` gives the result.
4. Cold, the stage copies the firmware's ATAGS or DTB to `0x00101000`, as a first stage does.
5. The screen. See [Screen](#screen). `screen` reports the path it took.
6. The tick, the console input and the maintenance port start. `input` reports them.
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
| Any key | Stops the countdown |

Keys come from the PL011, as ANSI sequences, and from the maintenance port's `key` and `char` commands.

The countdown boots the default entry no sooner than `timeout` seconds after the menu shows, by `timer_now_ms()`. The menu loop waits for input at most until the next second of the countdown, so the boot starts on the first pass after the time is up. The countdown uses the unsigned difference of two `timer_now_ms()` values, so it also works across the wrap of that counter after 2^32 ms.

### 5. Load and handoff

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
4. `bootinfo_build()` fills the block at `0x00100000` with stage id 2. Then the stage writes `boot_dev` (1, SD on EMMC), `boot_part_lba`, `boot_path` and `cmdline`, clears the fallback flag and reseals the block. Other flags stay as the previous stage set them.
5. `robco_handoff()` enters the image with r2 set to the firmware blob copy.

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
| `input` | `serial mini script` | Return codes of the input back ends |
| `selfcheck` | `rc size` | Result of `rimg_self_check()` |
| `mount` | `lba blocks` | The boot volume mounted |
| `config` | `result path`, then `entries ignored dup unknown bad clamped default_found nopath`, or `why` | `result` is `ok`, `missing` or `bad`. With `bad`, `why` is `read` with `rc`, `syntax` with `line`, or `empty` with the counters except `default_found`. See [Counters](#counters). |
| `entries` | `count default timeout` | The menu in use. `timeout` is `none` when the menu waits. |
| `countdown` | `state`, `seconds` | `state=start` when the menu shows with a timeout above 0, `state=stopped` when a key stops it |
| `ready` | | The menu shows |
| `select` | `index name` | The selection moved. `index` counts from 1. |
| `choose` | `index name path via` | An entry is to boot. `via` is `key` or `timeout`. |
| `error` | `code path name`, or `code path what rc` | A load failed, or `code=io` with `what=sd` or `what=fat` |
| `load` | `path size crc addr name subtype` | The image loaded and passed every check |
| `bootinfo` | `flags caps dev part_lba path cmdline_len scheme cx cy` | The boot info as handed over |
| `jump` | `entry name` | Last line before the handoff |
| `idle` | | The stage stopped and waits |
| `reboot` | `source` | `key` or `maint` |

## Error codes

The codes are the loader's, from [IMAGE_FORMAT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/c89d53ab9d600b530d18a49e0e8be570726ed825/docs/spec/IMAGE_FORMAT.md). The screen line is `ERROR E<code> <NAME>: <entry>: <reason>`.

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

- Unknown keys, keys before the first section and keys in unknown sections count as `unknown`. `[bios]` belongs to RETROS-BIOS and is skipped without a count.
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
| `boot_flags` after this stage | Fallback bit cleared, other bits kept |
| The screen after a handoff | Adopted, see [Screen](#screen) |
