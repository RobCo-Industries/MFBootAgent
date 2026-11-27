# MFBootAgent user manual

This manual is for a person with a Raspberry Pi, an SD card with the RobCo chain on it, and a serial cable or a screen.

Nothing here is validated on real hardware yet. Every behaviour below was seen in QEMU only. The GPIO buttons are tested in QEMU only, see [Buttons](#buttons).

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

At start the stage samples the pins for 100 ms. If every button reads pressed, it takes that as no buttons wired and stops reading them. The keyboard on the serial line still works. A button held down at power-on does not stop the others, as long as one button reads released. A button still held when the check ends counts as one press once the menu shows. Hold BACK at power-on to open the maintenance screen. Hold SELECT to boot the default entry at once. When a maintenance request or the earlier stage opens the maintenance screen at start, a button held at power-on does nothing on it. Release the button and press again to use the screen.

The buttons are tested in QEMU only. The tests press them as clean level changes, with no bounce. No board with buttons has run this code.

The diagnostics image reads the same six pins, plus a dial. See [DIAG.md](DIAG.md). The maintenance image reads the same six pins. See [MAINT.md](MAINT.md).

### The countdown

With `timeout` set in `BOOT.CFG`, the status row counts down: `Booting UOS in 3 s. Press any key to stop.` At zero the default entry boots. Any key or button stops the countdown, and the row changes to `Countdown stopped. Press ENTER to boot.` The countdown never starts again during this boot, also not after the maintenance screen or a failed load. Without `timeout` the menu waits for a key. With `timeout = 0` the default boots at once.

## Starting an image

When you boot an entry, the stage shows `Loading <name> from <path>`. It checks the image header, loads the file into the OS slot at `0x00400000` and checks its CRC. Then it shows `Starting <name>.` and hands over. The next stage writes below that line.

## The maintenance image

The `MAINT` entry starts `MAINT.SYS`, the maintenance image. It needs no login: being at the terminal is the authority. It has six tools: check card, check images, view file, set clock, restore users and restart. Restore users puts back the default user file when every password is lost. It is off unless `BOOT.CFG` has `restore_users = on` in a `[maint]` section. The maintenance screen starts it too, also after a [maintenance request](#maintenance-request). [MAINT.md](MAINT.md) describes each tool.

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
- It opens by itself, in place of the countdown, after a [maintenance request](#maintenance-request) on the card.
- It opens by itself, in place of the countdown, when the previous stage passes on the request for maintenance mode. RETROS-BIOS does not make that request today.

Opening the screen stops the countdown.

```
MFBOOT AGENT V0.1.0 MAINTENANCE
> Verify entries
  System
  Configuration
  Start the maintenance image
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

A good `DIAG.SYS`, the diagnostics image, shows `OK` like the other operating images. `RESULT` is `OK` or the code a failed boot would show. The [error table](#error-lines) says what each code means. `E06 BOUNDS` covers both of its reasons. Boot the entry to see the full line.

Each check loads the image into the OS slot. Nothing runs there yet, so this is safe. A later boot loads the image again.

With no card, or no usable volume, the action shows one line instead of the table: `ERROR E02 IO: SD card or FAT volume not usable`. Put the card back and run the action again. The next run finds the card.

### Changing the card

You can pull the card and put it back, or put in another card, while the menu or this screen shows. Verify entries and every boot from the menu mount the card that is in the slot at that moment. They never read through the old mount.

The stage reads `/ROBCO/BOOT.CFG` once, at start. The menu, the Configuration page and Verify entries keep that list after a card change. Verify entries checks the old list against the new card. A boot loads the chosen entry's path from the new card. To use the entries of a new card, reboot.

### System

The page shows the state of the last mount. After a failed Verify entries or boot, it says the volume is not mounted. After a mount that works, it shows that volume again.

| Row | Shows |
|---|---|
| `Board` | The board model and its chip |
| `Memory` | ARM memory in MiB |
| `Boot volume` | The FAT volume mounted now: `SD card, block <first>, <size> blocks`. When nothing is mounted: `not mounted, step <step> failed, code <code>`. The step is `sd`, `fat` or `release`. |
| `Last good` | Only when nothing is mounted. The last volume that mounted, as `SD card, block <first>, <size> blocks`, or `none since power-on`. |
| `Boot info` | `trusted, from the previous stage`, or `none trusted, this stage cold-booted` when the stage started alone |
| `Boot flags` | The flags the previous stage passed on, in hex, or `none received` |
| `Version` | The version of this stage |
| `RobCo-Core` | The commit of the library this stage was built with, its first 12 characters |
| `Screen` | `adopted from the previous stage`, `started by this stage`, or `none, serial only` |

### Configuration

The file as the stage read it at start, and how it was used: `in use`, `missing`, `does not read`, a line that does not parse, or `has no usable entry`. In the last four cases the menu has one default entry. Then the default entry, the timeout, the number of entries, and the counts of ignored, duplicate and bad entry sections and of clamped timeouts. [BOOT.md](BOOT.md#counters) defines each count.

### Start the maintenance image

Starts the maintenance image, as if you chose it in the menu. It uses the path of the `MAINT` entry when `BOOT.CFG` has one. Without one, and with the default menu of a missing or broken `BOOT.CFG`, it uses `/ROBCO/MAINT.SYS`. So a damaged `BOOT.CFG` never locks you out of it. When the file is missing or bad, the usual [error line](#error-lines) shows under the menu.

The maintenance image's status row starts with `Started by a request.` when a request file asked for maintenance, and with `Maintenance asked.` otherwise. A button you held at power-on does nothing in it either. Release it and press again.

### Reboot

Restarts the board through the watchdog.

### Back to the boot menu

Same as ESC.

### What the next stage learns

The stage passes boot flags to the next stage.

- Bit 1 means the user asked for maintenance mode. The stage sets it when a maintenance request was found, when the maintenance screen opened during this boot, including when the previous stage passed bit 1 on, and when the image it starts is the maintenance image. Otherwise it is clear.
- Bit 3 means a maintenance request file asked for it. The stage sets it when a request was found this boot, whatever image you then start. Otherwise it is clear.

The maintenance image always gets bit 1. The diagnostics image ignores both bits.

### Maintenance request

UOS can ask for maintenance at the next start, for its command `SET HALT RESTART /MAINTENANCE`. It writes the file `/ROBCO/MAINT.REQ` on the card and restarts. You can also put an empty file of that name on the card from a computer.

At the next start this stage finds the file, reads it and deletes it. Then the menu shows with no countdown and the maintenance screen opens. Under the keys line it shows `MAINTENANCE REQUESTED` and the note in the file, such as who asked. A note is one line of up to 63 printable characters. A longer note, or one with other bytes, does not show, and the request still counts. No image starts until you pick one. The request works once: the next start is normal. So after you restart from the maintenance image, the boot menu shows as usual.

When the file cannot be deleted, for a locked card, a locked file or a card error, the screen adds a notice:

```
NOTICE: /ROBCO/MAINT.REQ could not be deleted, code -8.
The request comes back at each start until the file is removed.
```

Remove the file from a computer, or unlock the card, to stop it. A folder named `MAINT.REQ` is no request.

Holding BACK while a request waits gives the same screen, once.

The delete of this file is the only thing this stage ever writes on the card.

## BOOT.CFG

`/ROBCO/BOOT.CFG` is a text file on the card. RETROS-BIOS reads its `[bios]` section and the maintenance image its `[maint]` section. This stage reads `[boot]` and the `[entry NAME]` sections, and skips the others. The default file:

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
| The maintenance screen opens at each start with a notice | `/ROBCO/MAINT.REQ` cannot be deleted | Remove the file from a computer, or unlock the card |
| An entry you wrote is missing | Bad name, bad or missing path, repeated name, or more than 8 entries | Run Configuration and check the counts |
| The countdown does not run | No `timeout` key, a bad value, a key was pressed, or the previous stage asked for maintenance mode | Press ENTER to boot |
| An error line after ENTER | The image failed a check | See [Error lines](#error-lines). Run Verify entries to check every entry at once. |
| Buttons do nothing | No buttons wired, all read pressed at start, or wired to other pins | Use the serial keys. Check the pins in [Buttons](#buttons). |
