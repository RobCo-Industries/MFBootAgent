# Architecture

## Files

Pure files have no hardware access. The host tests link them, with RobCo-Core's `src/lib/`.

| File | Pure | What it holds |
|---|---|---|
| `src/main.c` | No | The flow: entry, screen, mount, `BOOT.CFG`, menu drawing, load, handoff |
| `src/maint.c` | No | Input back ends and the maintenance port commands `state`, `dump`, `reboot` |
| `src/mfb.h` | No | Shared state and output of `main.c` and `maint.c` |
| `src/bootcfg.c` | Yes | The `BOOT.CFG` parser on top of `ini.h` |
| `src/menu.c` | Yes | The menu state machine: selection, countdown, keys, dirty lines |
| `src/menulines.c` | Yes | Every line of the menu screen. The lore lines are here and marked as lore. |
| `src/boot.c` | Yes | Error names, reasons and lines, the accepted subtypes, the boot info fields this stage writes |
| `src/screen.c` | Yes | Reads the previous stage's text back from the framebuffer pixels |
| `src/robco_config.h` | | Image header: stage name `bootagent`, subtype 4, ABI 2, name `MFBOOT`, slot end |
| `boot/BOOT.CFG` | | The default `BOOT.CFG` for a card |
| `tests/host/` | | Host unit tests of the pure files |
| `tests/os/` | | The test operating image |
| `tests/*.py` | | QEMU target tests |

RobCo-Core sits in `external/robco-core` as a submodule. `mk/robco.mk` builds the image and `mk/host.mk` the host tests.

## Why the screen is read back

After a trusted handoff the framebuffer is live, but RobCo-Core's `fb.c` and `console.c` keep their state in the image that set them up. This stage has no call to adopt them. It calls `fb_init()` with the same mode, which gives the framebuffer driver its state, and `console_init()`, which clears the screen. Before that, `screen.c` matches each 8x16 cell against the console font in the four colour levels of the scheme. `main.c` then writes the cells again. A cell that matches nothing becomes `?` and counts in the `screen` marker.

## Memory

| Range | Use |
|---|---|
| `0x00100000`-`0x0010013F` | Boot info, read at entry and written before the jump |
| `0x00101000` | Firmware blob copy, written only on a cold boot |
| `0x00200000`-`0x003FFFFF` | This image, its BSS and its stacks: 2 MiB |
| `0x00400000`-`0x00FFFFFF` | OS slot, written by the loader |

The image is 71280 bytes. With BSS and stacks it takes 130272 bytes (`mem_size` `0x1FCE0`), about 6% of its 2 MiB range. These numbers come from one build with the toolchain on the development machine and change with the toolchain. The link fails if the image outgrows the range. The largest buffers are the `BOOT.CFG` text, 4 KiB, and the kept screen cells, 8 KiB. The code of this stage allocates nothing on a heap.
