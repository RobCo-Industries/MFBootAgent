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
| `make` or `make all` | `build/mfboot.img` and the test operating images in `build/os/` |
| `make CI=1 all` | The same with `-Werror` |
| `make test-host` | The pin check, the host unit tests with sanitizers, and RobCo-Core's GCC warning check of the host tests |
| `make test-target` | The QEMU tests. `BOARD=raspi2b` picks one board. `RETROS_IMG=<file>` adds the whole-chain test. |
| `make test` | `test-host`, then `test-target` |
| `make sdimage` | `build/sd.img`: `ROBCO/MFBOOT.SYS`, `ROBCO/BOOT.CFG` from `boot/`, and the test images as `UOS.SYS`, `MAINT.SYS` and `DIAG.SYS` |
| `make sdimage FIRST=<RETROS.IMG>` | The same card with the first stage as `RETROS.IMG` and its `config.txt`, so it boots the whole chain |
| `make run` | Boots `build/sd.img` in QEMU on this terminal. `BOARD=` picks the board, `raspi2b` by default. `WINDOW=1` opens the display. `FIRST=` boots the first stage instead of MFBootAgent alone. |
| `make check-pins` | The CI workflow pins and the doc links match the submodule |
| `make clean` | Removes `build/` |

The test operating images are for tests only. A real card needs real `UOS.SYS`, `MAINT.SYS` and `DIAG.SYS`.

## The image

`build/mfboot.img` is a sealed rimg: magic `BOOT`, subtype 4, ABI 2.0, linked at `0x00200000`. Copy it to the card as `/ROBCO/MFBOOT.SYS`.
