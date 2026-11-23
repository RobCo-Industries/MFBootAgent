# MFBootAgent user manual

This manual is for a person with a Raspberry Pi, an SD card with the RobCo chain on it, and a serial cable or a screen.

Nothing here is validated on real hardware yet. Every behaviour below was seen in QEMU only. The GPIO buttons are not tested at all, see [Buttons](#buttons).

## What MFBootAgent is

MFBootAgent is the second program of the RobCo terminal chain. RETROS-BIOS loads it from `/ROBCO/MFBOOT.SYS`. It shows a boot menu, loads the operating image you pick into memory and starts it. It can also check the images on the card from its maintenance screen.

## Where to look

The stage writes the same lines on the screen and on the serial line. The serial line is the PL011 at 115200 baud, 8 data bits, no parity, 1 stop bit, as for RETROS-BIOS. The BIOS manual shows the wiring.

After RETROS-BIOS the menu shows below the BIOS text, in the BIOS colours. Alone, the stage starts its own green screen.

## The boot menu

```
ROBCO MFBOOT AGENT V0.1.0
COPYRIGHT 2075-2077 ROBCO INDUSTRIES
MULTI-FUNCTION BOOT AGENT. SELECT AN OPERATING IMAGE.
========================================

> 1  UOS             DEFAULT  /ROBCO/UOS.SYS
  2  MAINT                    /ROBCO/MAINT.SYS
  3  DIAG                     /ROBCO/DIAG.SYS

UP/DOWN or 1-8: select   ENTER: boot   M: maintenance   R: reboot
Booting UOS in 3 s. Press any key to stop.
```

The first line has the version. The next two lines are lore text and mean nothing. Each entry row has its number, its name, `DEFAULT` for the default entry and its path. `>` marks the selected entry. A path too long for the row is cut at column 79.

### Keys on a serial terminal

| Key | Action |
|---|---|
| Up and down arrows | Move the selection. It stops at the first and last entry. |
| `1` to `8` | Select that entry, if it exists |
| ENTER | Boot the selected entry |
| `M` | Open the maintenance screen |
| `R` | Reboot the board |
| Any other key | Stops the countdown and does nothing else |

Letters work in either case. ESC only stops the countdown in the menu.

### Buttons

The stage reads six buttons, each from a GPIO pin to ground. It uses the pin's own pull-up.

| Button | Pin | In the menu | On the maintenance screen |
|---|---|---|---|
| UP | GPIO 5 | Move up | Move up |
| DOWN | GPIO 6 | Move down | Move down |
| SELECT | GPIO 26 | Boot the selection | Run the selected action |
| BACK | GPIO 27 | Open the maintenance screen | Back to the menu |
| LEFT | GPIO 17 | Nothing | Nothing |
| RIGHT | GPIO 22 | Nothing | Nothing |

Hold UP or DOWN and the bar moves again after half a second, then every 150 ms. The other buttons do not repeat.

At start the stage samples the pins for 100 ms. If every button reads pressed, it takes that as no buttons wired and stops reading them. The keyboard on the serial line still works. A button held down at power-on does not stop the others, as long as one button reads released.

The buttons are untested. QEMU cannot press them, so the tests cover only the key mapping and the start-up check. No board with buttons has run this code.

### The countdown

With `timeout` set in `BOOT.CFG`, the status row counts down: `Booting UOS in 3 s. Press any key to stop.` At zero the default entry boots. Any key or button stops the countdown, and the row changes to `Countdown stopped. Press ENTER to boot.` The countdown never starts again during this boot, also not after the maintenance screen or a failed load. Without `timeout` the menu waits for a key. With `timeout = 0` the default boots at once.

## Starting an image

When you boot an entry, the stage shows `Loading <name> from <path>`. It checks the image header, loads the file into the OS slot at `0x00400000` and checks its CRC. Then it shows `Starting <name>.` and hands over. The next stage writes below that line.

## Error lines

A failed load shows one line under the menu, `ERROR E<nn> <NAME>: <entry>: <reason>`. The menu stays. Pick another entry, or fix the card and press ENTER again.

| Code | Line ends with | What to do |
|---|---|---|
| `E01 NOFILE` | `file not found` | Copy the image to the path in the entry, or fix the `path` in `BOOT.CFG` |
| `E02 IO` | `SD card or FAT volume not usable` | Reseat the card. Check its FAT partition. |
| `E03 MAGIC` | `not a boot image` | The file is something else or damaged. Copy it again. |
| `E04 VERSION` | `unknown header version` | The image is for another loader. Rebuild it. |
| `E05 SIZE` | `size does not match the file` | The copy is incomplete. Copy it again. |
| `E06 BOUNDS` | `image does not fit the OS slot` | The image is linked for another address. Rebuild it for `0x00400000`. |
| `E06 BOUNDS` | `not an operating image` | The file is a boot stage or another kind of image. Only UOS, maintenance and diagnostics images boot from this menu. |
| `E07 ABI` | `wrong ABI version` | The image needs another ABI. Rebuild it. |
| `E08 CRC` | `image CRC does not match` | The file is damaged. Copy it again. |

## The halted state

With no card, or no FAT volume holding `/ROBCO/BOOT.CFG` or `/ROBCO/UOS.SYS`, the stage shows:

```
ERROR E02 IO: SD card or FAT volume not usable
BOOT HALTED. Fix the card and reboot.
```

It then waits. Keys do nothing. Power the board off, fix the card and power it on. The stage never reboots by itself.

## The maintenance screen

### Opening it

- Press `M` in the menu, or the BACK button.
- A test script can send `maint` on the maintenance port. This port exists in QEMU only.
- It opens by itself, in place of the countdown, when the previous stage passes on the request for maintenance mode. RETROS-BIOS does not make that request today.

Opening the screen stops the countdown.

```
MFBOOT AGENT V0.1.0 MAINTENANCE
========================================
> Verify entries
  System
  Configuration
  Reboot
  Back to the boot menu
UP/DOWN: move   ENTER: run   ESC or Q: back to the boot menu
```

UP and DOWN move the bar. ENTER or SELECT runs the action. ESC, `Q` or BACK goes back to the menu, which shows again below the screen. The output of an action shows in the ten rows under the keys line, and the next action replaces it.

When you open the screen yourself, the stage tells the next stage about it. See [What the next stage learns](#what-the-next-stage-learns).

### Verify entries

The stage mounts the card again, then runs every check of a boot on each entry, in menu order, without starting anything:

```
#  ENTRY           RESULT      PATH
1  UOS             OK          /ROBCO/UOS.SYS
2  MAINT           E08 CRC     /ROBCO/MAINT.SYS
3  DIAG            E01 NOFILE  /ROBCO/DIAG.SYS
3 entries checked: 1 good, 2 failed.
```

`RESULT` is `OK` or the code a failed boot would show. The [error table](#error-lines) says what each code means. `E06 BOUNDS` covers both of its reasons. Boot the entry to see the full line.

Each check loads the image into the OS slot. Nothing runs there yet, so this is safe. A later boot loads the image again.

With no card, or no usable volume, the action shows one line instead of the table: `ERROR E02 IO: SD card or FAT volume not usable`. A boot then fails with `E02` too, until a later verify finds the card again.

### System

| Row | Shows |
|---|---|
| `Board` | The board model and its chip |
| `Memory` | ARM memory in MiB |
| `Boot volume` | The first block and the size of the FAT volume in use |
| `Boot info` | `trusted, from the previous stage`, or `none trusted, this stage cold-booted` when the stage started alone |
| `Boot flags` | The flags the previous stage passed on, in hex, or `none received` |
| `Version` | The version of this stage |
| `RobCo-Core` | The commit of the library this stage was built with |
| `Screen` | `adopted from the previous stage`, `started by this stage`, or `none, serial only` |

### Configuration

The file and how it was used: `in use`, `missing`, `does not read`, a line that does not parse, or `has no usable entry`. In the last four cases the menu has one default entry. Then the default entry, the timeout, the number of entries, and the counts of ignored, duplicate and bad entry sections and of clamped timeouts. [BOOT.md](BOOT.md#counters) defines each count.

### Reboot

Restarts the board through the watchdog.

### Back to the boot menu

Same as ESC.

### What the next stage learns

The stage passes boot flags to the next stage. Bit 1 means the user asked for maintenance mode. The stage sets it when you opened the maintenance screen during this boot. It keeps it when the previous stage passed it on. Otherwise it is clear.

## BOOT.CFG

`/ROBCO/BOOT.CFG` is a text file on the card. RETROS-BIOS reads its `[bios]` section. This stage reads `[boot]` and the `[entry NAME]` sections. The default file:

```
[boot]
default = UOS
timeout = 3

[entry UOS]
path = /ROBCO/UOS.SYS
cmdline =

[entry MAINT]
path = /ROBCO/MAINT.SYS

[entry DIAG]
path = /ROBCO/DIAG.SYS
```

| Key | Meaning |
|---|---|
| `default` | The entry the countdown boots and the menu selects first. Any case. Without it, or with an unknown name, the first entry. |
| `timeout` | Seconds before the default boots, 0 to 99. A larger number becomes 99. Without it the menu waits. |
| `[entry NAME]` | One menu entry. The name has 1 to 15 letters, digits, `_` or `-`. |
| `path` | The image, as an absolute 8.3 path such as `/ROBCO/UOS.SYS`. Any case. |
| `cmdline` | Up to 127 printable characters the next stage receives. Empty by default. |

The menu takes the first 8 usable entries. An entry without a `path`, with a bad path or a repeated name, gets no row.

With `timeout = 0` in `[boot]` and the entries of the default file, the default entry boots at once, with no wait.

This one selects DIAG first. It has no timeout, so the menu waits:

```
[boot]
default = diag

[entry UOS]
path = /ROBCO/UOS.SYS
cmdline = console=tty0 quiet

[entry DIAG]
path = /robco/diag.sys
```

A missing file, a line that does not parse, or a file with no usable entry gives a menu with one entry, `UOS` at `/ROBCO/UOS.SYS`, and no countdown. The Configuration action says which case it was. [BOOT.md](BOOT.md#bootcfg) has the exact rules.

## Troubleshooting

| What you see | Cause | What to do |
|---|---|---|
| Nothing on the serial line after the BIOS | The BIOS did not start this stage | Read the BIOS error line and the BIOS manual |
| `BOOT HALTED` | No card or no usable FAT volume | Reseat the card, check `/ROBCO/BOOT.CFG` or `/ROBCO/UOS.SYS` is on it, then power cycle |
| Only one entry, `UOS`, and no countdown | `BOOT.CFG` is missing, does not parse or has no usable entry | Open the maintenance screen, run Configuration, fix the file |
| An entry you wrote is missing | Bad name, bad or missing path, repeated name, or more than 8 entries | Run Configuration and check the counts |
| The countdown does not run | No `timeout` key, a bad value, a key was pressed, or the previous stage asked for maintenance mode | Press ENTER to boot |
| An error line after ENTER | The image failed a check | See [Error lines](#error-lines). Run Verify entries to check every entry at once. |
| Buttons do nothing | No buttons wired, all read pressed at start, or wired to other pins | Use the serial keys. Check the pins in [Buttons](#buttons). |
