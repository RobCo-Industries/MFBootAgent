# Boot flow

MFBootAgent implements the second stage of [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/b2774bf9abeddb3503064999d3cc0461b490e0d0/docs/spec/BOOT_PROTOCOL.md) in RobCo-Core. That document is the contract. This page says what this stage does and which choices it made where the contract is open.

Every marker line has the form `@RC stage=bootagent ev=<event> ...` on the PL011.

## Steps

### 1. Entry

1. The self check runs first, before any code writes initialised data: `rimg_self_check()` over the image as loaded.
2. The PL011 starts and the `boot` marker prints.
3. The trust rule runs in `robco_start()`. `handoff trusted=0|1` gives the result.
4. Cold, the stage copies the firmware's ATAGS or DTB to `0x00101000`, as a first stage does.
5. The screen. After a trusted handoff with a 640x480 framebuffer and an 80x30 console, the stage reads the text of the previous stage back from the pixels, calls `fb_init()` and `console_init()` with the same mode and colour scheme, writes the text again and puts the cursor where it was. Cold, it starts a green screen. `screen` reports it.
6. The tick, the console input and the maintenance port start. `input` reports them.
7. `selfcheck` prints the result of step 1. A failed self check is information only: the boot goes on.

RobCo-Core has no call to take over a live framebuffer. `console_init()` clears the screen, so the text vanishes for the time it takes to write it again.

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

1. `loader_load()` reads the file into the OS slot, `0x00400000` to `0x00FFFFFF`, 12 MiB. It expects magic `BOOT` and ABI major 2.
2. The subtype must be an operating image: 0 UOS, 2 maintenance or 3 diagnostics. Any other subtype, a boot agent included, fails with code 6, `bounds`. IMAGE_FORMAT.md checks the subtype in step 4, whose code is `bounds`.
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
| `screen` | `fb console kept unknown scheme` | `kept=1`: the previous text stays. `unknown`: cells that matched no glyph. |
| `input` | `serial mini script` | Return codes of the input back ends |
| `selfcheck` | `rc size` | Result of `rimg_self_check()` |
| `mount` | `lba blocks` | The boot volume mounted |
| `config` | `result path`, then `entries ignored dup unknown bad clamped default_found`, or `why` | `result` is `ok`, `missing` or `bad`. With `bad`, `why` is `read` with `rc`, `syntax` with `line`, or `empty`. |
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

The codes are the loader's, from [IMAGE_FORMAT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/b2774bf9abeddb3503064999d3cc0461b490e0d0/docs/spec/IMAGE_FORMAT.md). The screen line is `ERROR E<code> <NAME>: <entry>: <reason>`.

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

INI text, read with RobCo-Core's `ini.h`. Section names, key names and entry names are case-insensitive. Entry names show as written.

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
| `[entry NAME]` | `path` | Absolute path, 2 to 63 characters, no spaces | None: the entry is dropped |
| `[entry NAME]` | `cmdline` | Up to 127 characters | Empty |

Rules:

- At most 8 entries, in file order. Later entries are counted as `ignored`.
- A name is 1 to 15 characters. A longer name, or `[entry]` with no name, drops the entry and counts as `bad`.
- When two entries have the same name, the first wins. The others count as `dup`.
- `timeout` follows the rule of `delay_ms` in BOOT_PROTOCOL.md: one or more ASCII digits and nothing else. A value above 99 becomes 99 and counts as `clamped`. A bad value counts as `bad` and the menu waits.
- `timeout = 0` boots the default at once. The menu shows first.
- A missing or unknown `default` means the first entry.
- Unknown keys, and keys in unknown sections, count as `unknown`. `[bios]` belongs to RETROS-BIOS and is skipped without a count.
- A `cmdline` over 127 characters is past the INI reader's value limit, so the whole file is bad.
- A line that does not parse makes the whole file bad. A file that parses but leaves no entry with a path counts as bad too, with `why=empty`.

## Spec gaps and choices

| Gap | Choice |
|---|---|
| Markers for this stage besides `boot`, `handoff`, `ready`, `load`, `error`, `jump` | `screen`, `input`, `selfcheck`, `mount`, `config`, `entries`, `countdown`, `select`, `choose`, `bootinfo`, `idle`, `reboot`, in the format of the spec |
| `config` is listed as a first-stage event | This stage uses the same name and `result` values, with its own counts |
| Entry limits | 8 entries, 15-character names, paths up to `boot_path` and `cmdline` up to `cmdline`, minus the NUL |
| Timeout rule | Seconds 0 to 99, digits only, no key means wait |
| A file that parses with no usable entry | Treated like a bad file: the one default entry |
| A boot agent or other non-OS subtype | Refused with code 6, `bounds` |
| Boot volume markers | `/ROBCO/BOOT.CFG` and `/ROBCO/UOS.SYS` |
| `boot_flags` after this stage | Fallback bit cleared, other bits kept |
| The text on the screen after a handoff | Read back from the pixels, see step 1 |
