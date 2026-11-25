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
| Size | 103112 bytes. With BSS and stacks, `mem_size` is `0x37770`. These numbers come from one build and change with the toolchain. |

## Starting it

From the boot menu, choose the `DIAG` entry. The default `BOOT.CFG` has it. The image trusts the boot info, adopts the screen of the agent, then clears it.

The agent boots subtype 3 from its menu. Its maintenance screen's Verify entries runs the same checks on `DIAG.SYS` and shows `OK` for a good image. `test_diag_chain.py` runs both on one card.

When the user opened the agent's maintenance screen, the agent sets `boot_flags` bit 1. In BOOT_PROTOCOL.md the bit means the user asked for maintenance mode. The spec gives a diagnostics image no rule for it, so the image ignores the bit. Its menu is the same with the bit set or clear.

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

Keys come from the serial console, the maintenance port and the GPIO buttons. The buttons use the pins of BOARD_SUPPORT.md in RobCo-Core: UP 5, DOWN 6, LEFT 17, RIGHT 22, SELECT 26, BACK 27, a dial on 23 and 24 with its push on 16. The GPIO backend turns itself off when every button reads pressed for its whole guard time. The agent reads the same six button pins and no dial.

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

The test writes every range from the end of its own image, BSS and stacks to the top of ARM RAM. The top comes from the boot info, else the mailbox. When both exist, the boot info's range must lie inside the mailbox's ARM memory, or the test fails first with `why=top` and the text "Boot info RAM past mailbox". A smaller range passes: an earlier stage may keep RAM back. It never writes the handoff block, the firmware blob copy, the framebuffer or the image. `dg_mem_ranges()` computes the ranges, cut to 4 KiB pages.

The patterns, in order:

1. Each word holds its own address.
2. Walking ones over the first 32 words of every page.
3. A checkerboard, `0xAAAAAAAA` and `0x55555555` by word.
4. The checkerboard inverted.

Each pattern is written over the whole range, then read back. A fault gives the pattern, the first bad address, what was written and what was read.

The MMU and caches are on. The memory guide of RobCo-Core maps all ARM RAM as cached write-back memory, so the test uses that map. Before it reads a 1 MiB step back, the test cleans and invalidates that step in the D-cache with `cache_flush_range()`. The last lines of a fill can still be dirty in the cache, and without the flush the check would compare cached values that never reached RAM. The `flushed` key gives the bytes flushed per pattern and must equal `bytes`. The `cache` key is 1 when the MMU and caches are on.

QEMU models no cache. The QEMU tests prove that the flush runs over every byte, not that it reaches RAM on a board. The QEMU times say nothing about the speed on a board.

Run all tests only the first 16 MiB.

"ok" means every word read back as written. It does not prove the RAM below the image, which earlier stages own.

### sd

The card is read through a write guard in front of its block device. The guard's write function counts the attempt and refuses it with `BLK_ERR_RO`. It never calls the card's write function, so the test can never write. The `writes` key reports the count, and a count above 0 fails the test.

The guard carries no read-only flag on purpose. `blk_write()` refuses a device flagged `BLK_FLAG_READONLY` before it calls the write function, so a flagged guard could never count an attempt. A host test sends writes through `blk_write()` and checks that the guard counts and refuses each one, and that the card sees none.

1. The card identity: type, manufacturer, OEM, product name, serial and size. A raw CID of all zeros or all ones fails with `why=cid`: a card that answered sends its own ID, so a blank one means the bus read no data. A card size of 0 already fails `sd_init()`. A boot volume that ends past the card fails at the mount, with `why=mount`.
2. The blocks of the boot volume, in reads of 64 blocks. A volume of up to 65536 blocks, 32 MiB, is read whole. A larger one is read as 1024 reads spread from its first block to its last. So the test reads at most 32 MiB. A failed read is retried block by block to find the bad blocks. The failed read still fails the test, even when every block then reads alone. Some cards and controllers fail only on multi-block reads, and the boot loader reads that way. `chunk_errors` counts the failed reads and `errors` the blocks that failed alone. The row shows `N chunk reads failed`, or `Bad N blk, M chunks` when blocks failed too.
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

The test reads width, height, depth, pixel order, pitch, virtual size, virtual offset and alpha mode from the mailbox. `dg_video_judge()` compares the first five with the framebuffer in use. A different size, depth or pitch fails with `why=mismatch`. A different pixel order fails with `why=order`, `mode_order` and `fb_order`: `fb_rgb()` draws with the framebuffer's order, so the colours would come out swapped. A virtual offset other than 0 fails with `why=offset`: the screen would show another part of the buffer. Then it draws:

colour bars, a grid every 40 pixels with a one-pixel border on all four edges, a grey ramp, solid red, green, blue, white and black.

Alone, a key steps to the next pattern, and each pattern waits at most 30 s. In run all each pattern shows for 300 ms. The test reads no pixel back. A person must judge the picture: "ok" only means the mode matches the framebuffer and every pattern was drawn. The QEMU tests read the pixels from a screenshot.

### audio

Five tones of 150 ms, then a six-note tune, through `audio.h`. The `backend` key says what the library chose. QEMU does not model the PWM, so the library uses its trace backend. That backend prints one `audio` marker per call. It proves the calls, not a sound. Each sound has a bound of its length plus 500 ms.

When `audio_init()` returns `AUDIO_ERR_NO_PWM` or `AUDIO_ERR_PIN`, the PWM failed and the trace backend took over. The test still plays through the trace, then fails with the `init` code, since no sound came out.

When the PWM probe finds no PWM to drive and `audio_init()` returns 0, as in QEMU, the test plays through the trace and reports "ok" with `backend=trace`. That "ok" proves the calls and their timing, not a sound. A person must listen on a board, and `backend=pwm` is the only result that drove the pins.

### input

Each event shows on the screen and as an `input_event` marker, with its source, type, code and value. A row per GPIO button shows pressed or released. The view leaves on ESC twice within 1 s, BACK held for 1.5 s, or 20 s with no event. A single BACK stays, so the BACK button can be tried. Run all skips it: `why=interactive`. "ok" means the view opened and closed. It never fails on a key that did not come: only a person knows which key they pressed, so `events` and the event rows are for that person.

### timer

The tick interrupt reads the system timer at its first tick and 100 ticks later, 1 s at 100 Hz. The limit is one tick period over the window, 10000 ppm. A missed tick fails. The judge also compares the rate the tick driver reports, `hz`, with the rate the image asked, `asked_hz`. A tick that runs steadily at another rate fails. `missed` counts every tick missed during the test. `window_missed` counts those between the first and the last tick, so `us` covers 100 plus `window_missed` periods. The test gives up after 3 s with no tick.

### rng

1024 words from the generator. "ok" means no word equals the one before and the count of set bits is within 512 of half. This is a sanity check, not a test of randomness.

### rtc

Reads the clock chip at `0x68`. It reports the chip type, the time and the oscillator-stop flag. A stopped oscillator fails: the time is not valid. The test never sets the time.

### expander

The two PCA9554 of the terminal controller board, at `0x20` and `0x21` on I2C1.

STRICTLY READ-ONLY. Relays and door locks may hang on these pins. The test reads the four registers of each chip with a one-byte register pointer and a read. It never writes an output, a direction or the polarity. `gpiox_attach()` writes the polarity register when it is not 0, so the test attaches a chip only when its polarity reads 0. Then the attach writes nothing. The image does not link `gpiox_config()`, `gpiox_write()` or `gpiox_write_port()`, and a test checks this.

A chip is absent only when it does not acknowledge the first read. A chip whose four registers all read `0xff` fails with `stuck<n>=1` and an `expander_err` marker with `at=stuck`. A fresh chip holds polarity 0, and `gpiox_attach()` writes 0 there, so all ones means a data line held high after the address byte. Any other failed read fails the test, as does a failed read in the live view.

A chip whose polarity reads not 0 is never attached. The test reads its input port raw, and that path is not an error. A chip whose polarity reads 0 is attached. When a read inside `gpiox_attach()` fails, the test counts a bus error, prints an `expander_err` marker with `at=attach`, adds the `attach<n>=error rc<n>` keys and fails.

Alone, a live view shows the input port of each chip, a row per line from `X0.0` to `X1.7`, so a person can press a sensor contact and see the bit move. Each change prints an `expander_in` marker. ESC leaves, as does one minute with no change and no key. Run all reports presence and the registers once.

## What each test checks and what it only reports

A test reports OK only for what it measured. Each value a test reads is part of the verdict, reported in a marker key, or both. The values below are reported only, with the reason a wrong value cannot mean a fault the test could see.

| Test | Part of the verdict | Reported only, and why |
|---|---|---|
| `mem` | Every word of every pattern, the ranges, the boot info's RAM inside the mailbox's | A boot info range smaller than the mailbox's: an earlier stage may keep RAM back. `flushed`, `cache`, `ms`: they describe the run |
| `sd` | `sd_init()`, a blank CID, the mount, every chunk and block read, every `/ROBCO` image, the directory read, write attempts | `mid`, `oid`, `pnm`, `psn`: any value a card sends is valid. The partition type: the mount accepts only FAT. `kib_s`, `ms`, `sampled` |
| `video` | Mailbox answer, width, height, depth, pitch, pixel order, virtual offset, every pattern drawn | Virtual size: the pitch and the offset already cover what the screen shows. Alpha mode: `fb_rgb()` writes alpha `0xff` and the library never sets the mode, so the test has no expected value. A wrong mode shows as a blank picture for the person. The picture: no pixel is read back |
| `audio` | `audio_init()`, every tone and the tune within its bound, a PWM that fell back to trace | `backend=trace` with `init=0`: no PWM was found to drive, see audio above. The sound itself: only a person hears it |
| `input` | Nothing past the view opening and closing | `events`, `why`, `gpio`: a missing key press is not a fault without a person who pressed it |
| `timer` | A tick within 3 s, the window within 10000 ppm, missed ticks, the rate asked against the rate the driver reports | `window_missed`: it explains `us` |
| `rng` | `rng_init()`, every read, a word equal to the one before, the count of set bits | Nothing else. A constant or all-zero generator fails on repeats or bits |
| `rtc` | I2C start, the chip, the halt or oscillator-stop flag, every time field in range, 12-hour mode decoded | `chip`, `time`: any valid time may be right |
| `expander` | Every register read, the attach, all four registers `0xff`, every read in the live view | Input, output, polarity and configuration values: an earlier stage or the wiring may set them. All zeros: every pin an output driven low reads that way |

The RTC range checks live in RobCo-Core's `rtc_regs_decode()`. A month of 13, a day of 0, a day past the month's end or a reserved bit set gives `RTC_ERR_DATA`, and the test fails with `why=read`.

Nothing here is validated on real hardware.

## Run all

Run all runs every test except `input`, then prints `diag_done` with the count of each result. ESC stops the current test with `why=abort`, and the tests after it are skipped with `why=abort` too. It takes about 10 s in QEMU with a 16 MiB card.

The `RUN ALL` row gives FAILED when a test failed. Else it gives SKIPPED when ESC stopped the run, ABSENT when no test gave OK, and OK otherwise.

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
| `expander_err` | `chip`, at the first failed read of a chip in the live view. `chip at=attach rc` when the attach failed. |
| `diag_drop` | `name why`. The port's `diag` was answered `ok` but did not run: `run` when a key started a run first, `reboot` when a reboot came first. |
| `reboot` | `source`, `key` or `maint` |

`result` is `ok`, `fail`, `absent` or `skipped`. A skipped test has `why`: `abort` or `interactive`.

| Test | Keys after `result` |
|---|---|
| `mem` | `ranges start end bytes patterns ms flushed cache`. A fault: `pattern addr expected found bytes`. |
| `sd` | `probe_rc` in the test build after `sdwrite`, `card mid oid pnm psn blocks vol_lba vol_blocks read sampled chunk_errors errors`, `first_bad` on an error, `ms kib_s files images bad_images others`, `bad_path bad_rc` for the first bad image, `dir_rc` on a directory error, `writes`. No `/ROBCO`: `files=0 dir=missing` in place of the file keys. No card: `why=init rc`. |
| `video` | `width height depth pitch order patterns judge=person` |
| `audio` | `backend probe init tones notes` |
| `input` | `events why gpio`. `why` is `back2`, `long` or `idle`. |
| `timer` | `ticks hz us ppm limit missed window_missed` |
| `rng` | `words repeats bits` |
| `rtc` | `addr chip halted time` |
| `expander` | `x0 in0 out0 pol0 cfg0`, `x0=absent` or `x0=error rc0`, the same for chip 1, `attach0=error rc0` when the attach failed, the same for chip 1, `view` after the live view, and `errors` when a read failed |

## Maintenance port

The port is the mini UART: in QEMU, the second serial port. On hardware it reaches no pin.

| Command | Effect |
|---|---|
| `state` | One line: `phase trusted fb sel test request pending`. `request` is the test the port asked for that has not started. `pending` is the count of input events not taken yet. |
| `diag <name>` | Starts one test. `diag all` runs all. `err busy` while a test runs, while a request waits or when the input queue is full. |
| `dump` | The screen text |
| `key`, `char`, `text` | The library's built-in input commands |
| `reboot` | Restarts the board through the watchdog |

`diag` only queues the request. The main loop runs the test, so the port keeps answering.

The request goes through the input queue behind the keys the port received before it, as the agent's `maint` does. The menu takes them in the order the port got them. `diag timer` then `key select` runs the timer test. The SELECT comes during the test, and the image drops it with the other keys. `key select` then `diag timer` runs the selected row. The image drops the request with `diag_drop why=run` when that run starts, so it never runs later. A reboot, from the `R` key or the port, drops a waiting request with `diag_drop why=reboot`. One rule holds everywhere: an action taken before the request comes up drops it with a marker.

The port answers between input events, so a reply can come before the image handled the keys sent ahead of it. The tests read the screen only after a `state` reply with `pending=0`.

The test build, `build/diagtest/diag.img`, adds `fault <addr>`. It flips one bit at that address after the memory test writes it. It also stops the memory test at 16 MiB. It also adds `sdwrite`: the next SD run sends one write through its guard and reports the result as `probe_rc`. The guard counts and refuses it, so that run fails with `writes=1` and the card does not change. That run clears the request, so later runs send no write. `sdmulti` makes the guard fail every multi-block read of the next SD run with `BLK_ERR_IO`, while single-block reads reach the card. That run fails with `chunk_errors` above 0 and `errors=0`, and clears the request. It is not shipped.
