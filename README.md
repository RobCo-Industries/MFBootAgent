# MFBootAgent

MFBootAgent is the second stage of the terminal chain of the RobCo suite. The Pi firmware loads RETROS-BIOS, which loads `/ROBCO/MFBOOT.SYS`. MFBootAgent shows a boot menu, loads the chosen operating image into the OS slot and hands off to it. It runs on 32-bit Raspberry Pi boards and is built on the shared [RobCo-Core](https://github.com/RobCo-Industries/RobCo-Core) library.

The project is inspired by the RobCo terminals of the Fallout games. The title and the two lines under it on the menu screen are lore text written for this project. Everything else on the screen is real: the version, the entries, the paths, the keys, the countdown and the errors.

## Status

What works, in QEMU on `raspi0`, `raspi1ap`, `raspi2b` and `raspi3b`:

- It boots alone, loaded at `0x00200000` with no first stage.
- It boots after a first stage: RobCo-Core's `examples/chain1` or RETROS-BIOS. The screen continues below the first stage's text.
- It reads `/ROBCO/BOOT.CFG` and shows up to 8 entries, with a countdown when `timeout` is set.
- It loads the chosen entry, checks it and hands off with the boot info.
- A failed load shows an error line and the menu stays.

What does not exist yet: the maintenance mode, the diagnostics image and GPIO button navigation.

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
- [CONTRIBUTING.md](CONTRIBUTING.md) and [CHANGELOG.md](CHANGELOG.md).

## Licence

MIT. See [LICENSE](LICENSE).
