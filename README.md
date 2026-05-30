# Retro-Go S31

ESP32-S31-Korvo-1 port of Retro-Go, packaged as an ESP-IDF-friendly project.

This tree targets the ESP32-S31-Korvo-1 board with the ESP32-S3-LCD-EV-Board-SUB3
800x480 RGB LCD and touch panel. It builds a multi-app Retro-Go image containing:

- `launcher`
- `retro-core` for NES, Game Boy, Game Boy Color, Game & Watch, SNES and other
  Retro-Go cores
- `gbsp` for Game Boy Advance

## Status

This is a work-in-progress shareable version of the local S31 port.

Known issue:

- Audio output on the ES8389 path is not fully fixed yet. Some games produce
  sound effects, but continuous BGM can be incomplete or unstable. Please open
  an issue with the game/core name, what you hear, and a serial log if you can.

ROMs, BIOS files, cover packs and SD card contents are not included.

## Hardware

- ESP32-S31-Korvo-1
- ESP32-S3-LCD-EV-Board-SUB3 4.3-inch 800x480 RGB LCD
- Touch controller compatible with the board resource package
- FAT32 SD card
- Optional speaker connected to one of the board speaker outputs

## Build

Use an ESP-IDF environment that supports `esp32s31`.

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32s31
idf.py build
```

The root project is an ESP-IDF wrapper. A normal `idf.py build` builds and packs
the full Retro-Go image.

Useful custom targets:

```sh
idf.py rg-build-apps   # build launcher, retro-core and gbsp
idf.py rg-image        # build and pack the full image
idf.py rg-flash        # build, pack and flash
idf.py rg-monitor      # open serial monitor
idf.py rg-clean        # remove app build outputs
```

## Flash

Auto-detect port:

```sh
idf.py flash
```

Explicit port example:

```sh
idf.py -p /dev/cu.usbserial-10 flash
```

Direct image flashing example:

```sh
esptool.py --chip esp32s31 -p /dev/cu.usbserial-10 -b 1500000 \
  write_flash --flash_size detect 0x0 retro-go-s31_*.img
```

If 1500000 baud is unstable on your cable, try:

```sh
ESPBAUD=921600 idf.py flash
```

## SD Card Layout

Format the SD card as FAT32. Put ROMs on the card; do not commit them to this
repository.

Recommended layout:

```text
/
├── retro-go/
│   ├── bios/
│   │   ├── gb_bios.bin
│   │   ├── gbc_bios.bin
│   │   ├── fds_bios.bin
│   │   └── msx/
│   ├── config/
│   └── saves/
├── roms/
│   ├── nes/
│   ├── gb/
│   ├── gbc/
│   ├── gba/
│   ├── gw/
│   ├── sms/
│   ├── gg/
│   ├── sg/
│   ├── coleco/
│   ├── pce/
│   ├── lynx/
│   ├── snes/
│   ├── md/
│   └── msx/
└── romart/
    ├── nes/
    ├── gb/
    ├── gbc/
    ├── gba/
    └── ...
```

Notes:

- NES games go in `/roms/nes/`.
- Game Boy games go in `/roms/gb/`.
- Game Boy Color games go in `/roms/gbc/`.
- Game Boy Advance games go in `/roms/gba/`.
- Game & Watch games go in `/roms/gw/`; they must be packed with
  LCD-Game-Shrinker before use.
- Cover images go under `/romart/<system>/`.
- BIOS files are optional except for systems or games that require them.

Cover art may be filename-based:

```text
/romart/nes/Super Mario Bros.png
```

or CRC32-based:

```text
/romart/nes/A/ABCDE123.png
```

Filename-based covers are faster to browse on this board.

## Wi-Fi

For builds with networking enabled, create:

```text
/retro-go/config/wifi.json
```

Example:

```json
{
  "ssid0": "your-wifi-name",
  "password0": "your-wifi-password"
}
```

The default shared build command used during bring-up disables networking to
save memory:

```sh
python rg_tool.py --target esp32-s31-korvo-1 install --no-networking launcher retro-core gbsp
```

## Development Notes

More S31-specific details are in [README_S31.md](README_S31.md).

Original Retro-Go documentation is still useful for emulator behavior, theming
and porting:

- [BUILDING.md](BUILDING.md)
- [PORTING.md](PORTING.md)
- [THEMING.md](THEMING.md)
- [LOCALIZATION.md](LOCALIZATION.md)

## Legal

This repository does not include commercial ROMs or copyrighted game dumps.
Only add files you have the right to redistribute.

The project is based on Retro-Go and follows its license terms. See [COPYING](COPYING).

## Issues

Issues and pull requests are welcome, especially for:

- ES8389 audio fixes
- ESP32-S31-Korvo-1 board support improvements
- SD card compatibility
- emulator compatibility notes
