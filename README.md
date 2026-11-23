# MFBootAgent

MFBootAgent is the second stage of the terminal chain of the RobCo suite. The Pi firmware loads RETROS-BIOS, which loads `/ROBCO/MFBOOT.SYS`. MFBootAgent shows a boot menu, loads the chosen operating image into the OS slot and hands off to it. It runs on 32-bit Raspberry Pi boards and is built on the shared [RobCo-Core](https://github.com/RobCo-Industries/RobCo-Core) library.

The project is inspired by the RobCo terminals of the Fallout games. The title and the two lines under it on the menu screen are lore text written for this project. Everything else on the screen is real: the version, the entries, the paths, the keys, the countdown and the errors.

## Status

What works, in QEMU on `raspi0`, `raspi1ap`, `raspi2b` and `raspi3b`:

- It boots alone, loaded at `0x00200000` with no first stage.
- It boots after a first stage: RobCo-Core's `examples/chain1` or RETROS-BIOS. It adopts the first stage's screen and shows the menu below its text.
- It reads `/ROBCO/BOOT.CFG` and shows up to 8 usable entries, with a countdown when `timeout` is set.
- It checks the chosen image's header and subtype, loads it and hands off with the boot info.
- A failed load shows an error line and the menu stays.

It also builds `DIAG.SYS`, a diagnostics image with nine hardware tests. See [docs/DIAG.md](docs/DIAG.md).

What does not exist yet: the maintenance mode and GPIO button navigation in the agent.

Nothing has been tried on real hardware.

Not planned: TFTP boot, USB boot, holotape boot and image signatures.

## Build, run and test

You need `arm-none-eabi-gcc`, GNU make, Python 3, QEMU and mtools. See [docs/BUILD.md](docs/BUILD.md).

```sh
git submodule update --init
make                     # build/mfboot.img and the test operating images
make test-host           # host unit tests and the pin check
make test-target         # QEMU tests on every board
make sdimage             # build/sd.img
make run BOARD=raspi2b   # boot build/sd.img in QEMU
```

## Documents

- [docs/BOOT.md](docs/BOOT.md): the boot flow, the markers, the error codes and `BOOT.CFG`.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the files and the memory use.
- [docs/BUILD.md](docs/BUILD.md): tools, targets and the card.
- [docs/TESTING.md](docs/TESTING.md): how the tests start the stage and what they check.
- [docs/DIAG.md](docs/DIAG.md): the diagnostics image, its tests and markers.
- [CONTRIBUTING.md](CONTRIBUTING.md) and [CHANGELOG.md](CHANGELOG.md).

## Licence

MIT. See [LICENSE](LICENSE).
