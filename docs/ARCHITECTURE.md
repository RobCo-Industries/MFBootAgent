# Architecture

## Files

Pure files have no hardware access. The host tests link them, with RobCo-Core's `src/lib/`.

| File | Pure | What it holds |
|---|---|---|
| `src/main.c` | No | The flow: entry, screen adoption, mount, `BOOT.CFG`, menu drawing, header read, load, handoff |
| `src/maint.c` | No | Input back ends and the maintenance port commands `state`, `dump`, `reboot` |
| `src/mfb.h` | No | Shared state and output of `main.c` and `maint.c` |
| `src/bootcfg.c` | Yes | The `BOOT.CFG` parser on top of `ini_parse_sections()` |
| `src/menu.c` | Yes | The menu state machine: selection, countdown, keys, dirty lines |
| `src/menulines.c` | Yes | Every line of the menu screen. The lore lines are here and marked as lore. |
| `src/boot.c` | Yes | Error names, reasons and lines, the header check up to the subtype, the boot info fields this stage writes |
| `src/robco_config.h` | | Image header: stage name `bootagent`, subtype 4, ABI 2, name `MFBOOT`, slot end |
| `boot/BOOT.CFG` | | The default `BOOT.CFG` for a card |
| `tests/host/` | | Host unit tests of the pure files |
| `tests/os/` | | The test operating image |
| `tests/*.py` | | QEMU target tests |

RobCo-Core sits in `external/robco-core` as a submodule. `mk/robco.mk` builds the image and `mk/host.mk` the host tests.

## The screen

After a trusted handoff `main.c` adopts the live framebuffer and console with `fb_adopt()` and `console_adopt()`. No pixel changes. The console does not know the earlier characters, so the menu writes only its own rows. [BOOT.md](BOOT.md#screen) lists the paths.

## Memory

| Range | Use |
|---|---|
| `0x00100000`-`0x0010013F` | Boot info, read at entry and written before the jump |
| `0x00101000` | Firmware blob copy, written only on a cold boot |
| `0x00200000`-`0x003FFFFF` | This image, its BSS and its stacks: 2 MiB |
| `0x00400000`-`0x00FFFFFF` | OS slot, written by the loader |

The image is 72160 bytes. With BSS and stacks it takes 130704 bytes (`mem_size` `0x1FE90`), about 6% of its 2 MiB range. These numbers come from one build with the toolchain on the development machine and change with the toolchain. The link fails if the image outgrows the range. The largest buffers are the console cells of RobCo-Core, 8 KiB, the parser state with the names of up to 410 entry headers, 7 KiB, and the `BOOT.CFG` text, 4 KiB. The code of this stage allocates nothing on a heap.

## The diagnostics image

`diag/` builds `DIAG.SYS` with its own `Makefile` and `robco_config.h`. The pure files hold the decisions and have host tests: `dg_mem.c` the RAM ranges and patterns, `dg_sd.c` the read plan, the file rule and the write guard, `dg_menu.c` the menu and the live view exit rule, `dg_fmt.c` the lines, markers, verdicts, the run-all result and the expander attach rule. `main.c` holds the entry, the screen, the menu loop and the port. Each `t_*.c` file is a thin layer over the drivers for one or more tests. [DIAG.md](DIAG.md) describes the image.
