# Diagnostics image

`DIAG.SYS` checks the hardware of a terminal. It runs the long and interactive tests. RETROS-BIOS runs the short, bounded ones at power-on. The sources are in `diag/`.

Every number on this page comes from QEMU. Nothing has been tried on real hardware.

## The image

| Field | Value |
|---|---|
| File on the card | `/ROBCO/DIAG.SYS`, built as `build/diag.sys` |
| Magic, subtype, ABI | `BOOT`, 3, 2.0 |
| Link address | `0x00400000`, the OS slot |
| Header name, version | `DIAG`, 0.1.0 |
| Size | 101896 bytes. With BSS and stacks, `mem_size` is `0x37770`. |

## Starting it

From the boot menu, choose the `DIAG` entry. The default `BOOT.CFG` has it. The image trusts the boot info, adopts the screen of the agent, then clears it.

Alone, a two-word image at `0x8000` jumps to `0x00400000`. `make run-diag` does this in QEMU with the card. `DEVICES=1` adds the clock chip and both expanders. Alone, the image starts a fresh 640 by 480 screen.

The colour scheme comes from the boot info, else green.

## The menu

| Key | Effect |
|---|---|
| UP, DOWN | Move the selection |
| ENTER or SELECT | Start the selected row |
| `1` to `9` | Start that test |
| `A` | Run all |
| `R` | Reboot through the watchdog |
| ESC or BACK | Stop a test |

Keys come from the serial console, the maintenance port and the GPIO buttons. The buttons use the pins of BOARD_SUPPORT.md in RobCo-Core: UP 5, DOWN 6, LEFT 17, RIGHT 22, SELECT 26, BACK 27, a dial on 23 and 24 with its push on 16. The GPIO backend turns itself off when every button reads pressed for its whole guard time.

Each row of the menu shows the last result of its test and a short detail. The detail holds 26 characters at most, so the row fits 79 columns. The console keeps column 80 blank. The work rows and the `diag` marker give the full detail. The status row under the help line shows the last test or the run-all summary. A test writes its lines in the area under the menu.

## The tests

| Test | What it does | Time in QEMU |
|---|---|---|
| `mem` | Writes each pattern over the RAM, then reads it all back | 4 s for 448 MiB, 9 s for 960 MiB |
| `sd` | Card identity, block reads of the boot volume, image file checks | 4 s for a 16 MiB card |
| `video` | Mode from the mailbox, then eight full-screen patterns | 2.4 s in run all |
| `audio` | Five tones from 250 Hz to 4000 Hz, then a six-note tune | 2 s |
| `input` | A live view of every key | Until the person leaves |
| `timer` | The system timer against 100 ticks of the tick interrupt | 1 s |
| `rng` | 1024 words from the hardware generator | Under 0.1 s |
| `rtc` | Reads the DS1338 at `0x68` | Under 0.1 s |
| `expander` | Reads the two PCA9554 at `0x20` and `0x21` | Under 0.1 s, then the live view |

### mem

The test writes every range from the end of its own image, BSS and stacks to the top of ARM RAM. The top comes from the boot info, else the mailbox. It never writes the handoff block, the firmware blob copy, the framebuffer or the image. `dg_mem_ranges()` computes the ranges, cut to 4 KiB pages.

The patterns, in order:

1. Each word holds its own address.
2. Walking ones over the first 32 words of every page.
3. A checkerboard, `0xAAAAAAAA` and `0x55555555` by word.
4. The checkerboard inverted.

Each pattern is written over the whole range, then read back. A fault gives the pattern, the first bad address, what was written and what was read.

The MMU and caches are on. The memory guide of RobCo-Core maps all ARM RAM as cached write-back memory, so the test uses that map. The ranges are hundreds of MiB, far larger than the caches, so the reads come from RAM. QEMU models no cache, so the QEMU times say nothing about the speed on a board.

Run all tests only the first 16 MiB.

"ok" means every word read back as written. It does not prove the RAM below the image, which earlier stages own.

### sd

The card is read through a copy of its block device flagged read-only. Its write function only counts the attempt, so the test can never write. The `writes` key reports the count, and a count above 0 fails the test.

1. The card identity: type, manufacturer, OEM, product name, serial and size.
2. The blocks of the boot volume, in reads of 64 blocks. A volume of up to 65536 blocks, 32 MiB, is read whole. A larger one is read as 1024 reads spread from its first block to its last. So the test reads at most 32 MiB. A failed read is retried block by block to find the first bad block.
3. The files of `/ROBCO`, by the rule below.

The rule that names images: every entry of `/ROBCO` whose extension is `.SYS`, `.BAK` or `.IMG`, in any case, must be an image. The test reads such a file into RAM above the OS slot and checks it with `rimg_validate()`, the loader's own steps. Each image prints an `sd_image` marker and a work row. Its `rc` is one of these:

| `rc` | Cause |
|---|---|
| `ok` | The image passed every step |
| `empty` | The file has 0 bytes |
| `short` | The file has 1 to 95 bytes, too few for a header |
| `magic` | The magic is not a known one |
| `version`, `size`, `bounds`, `abi`, `crc` | That step of `rimg_validate()` failed |
| `isdir` | A directory has an image name |
| `open` | The file does not open |
| `io` | A read of the file failed or came back short |

Any other file is listed as "not an image". The test does not read it, and it never fails the test. Other directories are ignored.

A read error of the directory fails the test with `dir_rc`, the error code. It never counts as the end of the directory. A card without `/ROBCO` fails with `dir=missing` and the text "No /ROBCO folder". The block reads still run and report.

"ok" means no read error, a whole `/ROBCO`, and every image passed. With no card, the result is `absent`.

### video

The test reads width, height, depth, pixel order and pitch from the mailbox and compares them with the screen in use. Then it draws:

colour bars, a grid every 40 pixels with a one-pixel border on all four edges, a grey ramp, solid red, green, blue, white and black.

Alone, a key steps to the next pattern, and each pattern waits at most 30 s. In run all each pattern shows for 300 ms. A person must judge the picture: "ok" only means the mode reads back and every pattern was drawn.

### audio

Five tones of 150 ms, then a six-note tune, through `audio.h`. The `backend` key says what the library chose. QEMU does not model the PWM, so the library uses its trace backend. That backend prints one `audio` marker per call. It proves the calls, not a sound. Each sound has a bound of its length plus 500 ms.

### input

Each event shows on the screen and as an `input_event` marker, with its source, type, code and value. A row per GPIO button shows pressed or released. The view leaves on ESC twice within 1 s, BACK held for 1.5 s, or 20 s with no event. A single BACK stays, so the BACK button can be tried. Run all skips it: `why=interactive`.

### timer

The tick interrupt reads the system timer at its first tick and 100 ticks later, 1 s at 100 Hz. The limit is one tick period over the window, 10000 ppm. A missed tick fails. The test gives up after 3 s with no tick.

### rng

1024 words from the generator. "ok" means no word equals the one before and the count of set bits is within 512 of half. This is a sanity check, not a test of randomness.

### rtc

Reads the clock chip at `0x68`. It reports the chip type, the time and the oscillator-stop flag. A stopped oscillator fails: the time is not valid. The test never sets the time.

### expander

The two PCA9554 of the terminal controller board, at `0x20` and `0x21` on I2C1.

STRICTLY READ-ONLY. Relays and door locks may hang on these pins. The test reads the four registers of each chip with a one-byte register pointer and a read. It never writes an output, a direction or the polarity. `gpiox_attach()` writes the polarity register when it is not 0, so the test attaches a chip only when its polarity reads 0. Then the attach writes nothing. The image does not link `gpiox_config()`, `gpiox_write()` or `gpiox_write_port()`, and a test checks this.

A chip is absent only when it does not acknowledge the first read. Any other failed read fails the test, as does a failed read in the live view.

Alone, a live view shows the input port of each chip, a row per line from `X0.0` to `X1.7`, so a person can press a sensor contact and see the bit move. Each change prints an `expander_in` marker. ESC leaves, as does one minute with no change and no key. Run all reports presence and the registers once.

## Run all

Run all runs every test except `input`, then prints `diag_done` with the count of each result. ESC stops the current test with `why=abort`, and the tests after it are skipped with `why=abort` too. It takes about 10 s in QEMU with a 16 MiB card.

## Markers

The stage name is `diag`. Each line starts with `@RC stage=diag`.

| Event | Keys |
|---|---|
| `boot` | `midr soc base board_rev mem entry_mode version` |
| `handoff` | `trusted` |
| `screen` | `fb console path scheme cols rows` |
| `mmu` | `rc on` |
| `input` | `serial mini script gpio` |
| `selfcheck` | `rc size` |
| `fault_hook` | `name`, test builds only |
| `ready` | The menu is up |
| `select` | `index name` |
| `diag` | `name result`, then the test keys below |
| `diag_done` | `ok fail absent skipped` |
| `diag_progress` | `name step pct`, at most one per 500 ms |
| `sd_image` | `path size rc` |
| `video_pattern` | `name width height` |
| `input_view` | `state gpio idle_ms` |
| `input_event` | `source type code value` |
| `i2c` | `rc`, at the first I2C test |
| `expander` | `chip addr present`, then `in out pol cfg attached` or `rc` |
| `expander_in` | `chip bits` |
| `expander_err` | `chip`, at the first failed read of a chip in the live view |
| `reboot` | `source`, `key` or `maint` |

`result` is `ok`, `fail`, `absent` or `skipped`. A skipped test has `why`: `abort` or `interactive`.

| Test | Keys after `result` |
|---|---|
| `mem` | `ranges start end bytes patterns ms`. A fault: `pattern addr expected found bytes`. |
| `sd` | `card mid oid pnm psn blocks vol_lba vol_blocks read sampled errors`, `first_bad` on an error, `ms kib_s files images bad_images others`, `bad_path bad_rc` for the first bad image, `dir_rc` on a directory error, `writes`. No `/ROBCO`: `files=0 dir=missing` in place of the file keys. No card: `why=init rc`. |
| `video` | `width height depth pitch order patterns judge=person` |
| `audio` | `backend probe init tones notes` |
| `input` | `events why gpio`. `why` is `back2`, `long` or `idle`. |
| `timer` | `ticks hz us ppm limit missed` |
| `rng` | `words repeats bits` |
| `rtc` | `addr chip halted time` |
| `expander` | `x0 in0 out0 pol0 cfg0`, `x0=absent` or `x0=error rc0`, the same for chip 1, `view` after the live view, and `errors` when a read failed |

## Maintenance port

The port is the mini UART: in QEMU, the second serial port. On hardware it reaches no pin.

| Command | Effect |
|---|---|
| `state` | One line: `phase sel test trusted fb pending` |
| `diag <name>` | Starts one test. `diag all` runs all. `err busy` while a test runs. |
| `dump` | The screen text |
| `key`, `char`, `text` | The library's built-in input commands |
| `reboot` | Restarts the board through the watchdog |

`diag` only records the request. The main loop runs the test, so the port keeps answering.

The test build, `build/diagtest/diag.img`, adds `fault <addr>`. It flips one bit at that address after the memory test writes it. It also stops the memory test at 16 MiB. It is not shipped.
