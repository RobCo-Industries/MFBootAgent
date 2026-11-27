# Build

## Tools

- `arm-none-eabi-gcc` and binutils
- GNU make
- Python 3
- QEMU with `qemu-system-arm` and `qemu-system-aarch64`, for the target tests and `make run`
- mtools, for the cards

CI runs in RobCo-Core's container image, pinned in `.github/workflows/ci.yml`.

## Targets

| Target | What it does |
|---|---|
| `make` or `make all` | `build/mfboot.img`, its test build in `build/mfbtest/`, `build/diag.sys`, its test build in `build/diagtest/`, `build/maint.sys`, its test build in `build/mainttest/` and the test operating images in `build/os/` |
| `make CI=1 all` | The same with `-Werror` |
| `make test-host` | The pin check, the workflow guard, the host unit tests with sanitizers, and RobCo-Core's GCC warning check of the host tests |
| `make test-target` | The QEMU tests. `BOARD=raspi2b` picks one board. `RETROS_IMG=<file>` adds the whole-chain test. |
| `make test` | `test-host`, then `test-target` |
| `make sdimage` | `build/sd.img`: `ROBCO/MFBOOT.SYS`, `ROBCO/BOOT.CFG` from `boot/`, the test image as `UOS.SYS`, `build/maint.sys` as `MAINT.SYS` and `build/diag.sys` as `DIAG.SYS` |
| `make sdimage FIRST=<RETROS.IMG>` | The same card with the first stage as `RETROS.IMG` and its `config.txt`, so it boots the whole chain |
| `make run` | Boots `build/sd.img` in QEMU on this terminal. `BOARD=` picks the board, `raspi2b` by default. `WINDOW=1` opens the display. `FIRST=` boots the first stage instead of MFBootAgent alone. |
| `make check-pins` | The CI workflow pins and the doc links match the submodule, then `make check-workflows` |
| `make check-workflows` | RobCo-Core's `tools/check_workflow_exprs.py` on every file under `.github/workflows/`. It fails on a `${{ }}` that could reach a script and on a `uses:` not pinned by a full commit. CI runs it through `make test-host`. |
| `make -s print-core-commit` | The library commit the System screen shows: the commit of `external/robco-core`, or the CI workflow pin when git cannot read it. `MFB_CORE_PIN=<value>` on the command line sets it. |
| `make clean` | Removes `build/` |
| `make run-diag` | Boots `build/diag.sys` alone in QEMU with `build/sd.img`. `DEVICES=1` adds the clock chip and both expanders. |
| `make run-maint` | Boots `build/maint.sys` alone in QEMU with `build/sd.img`. `DEVICES=1` adds the clock chip and both expanders. |

The test operating images are for tests only. A real card needs a real `UOS.SYS`.

## The image

`build/mfboot.img` is a sealed rimg: magic `BOOT`, subtype 4, ABI 2.0, linked at `0x00200000`. Copy it to the card as `/ROBCO/MFBOOT.SYS`.

## The diagnostics image

`build/diag.sys` is a sealed rimg: magic `BOOT`, subtype 3, ABI 2.0, linked at `0x00400000`. Copy it to the card as `/ROBCO/DIAG.SYS`. The link fails if it outgrows the OS slot. See [DIAG.md](DIAG.md).

## The maintenance image

`build/maint.sys` is a sealed rimg: magic `BOOT`, subtype 2, ABI 2.0, linked at `0x00400000`. Copy it to the card as `/ROBCO/MAINT.SYS`. The link fails if it outgrows the OS slot. It also compiles `diag/dg_sd.c`, `diag/dg_sdimg.c`, `diag/dg_fmt.c` and `src/portreq.c`. See [MAINT.md](MAINT.md).
