# Retro-Go S31 Quick Start

This fork packages Retro-Go for the ESP32-S31-Korvo-1 with the
ESP32-S3-LCD-EV-Board-SUB3 800x480 RGB LCD.

The firmware is still a Retro-Go multi-app image. The repository root is an
ESP-IDF wrapper project so it can be opened by the ESP-IDF VS Code extension and
used from the root directory.

The root ESP-IDF `build`, `flash`, and `app-flash` targets are wired to the
Retro-Go image. The tiny root app still exists only to make ESP-IDF recognize
the repository, but ESP-IDF flashing writes the full staged image at offset 0x0.

## Hardware

- Board: ESP32-S31-Korvo-1
- Display: ESP32-S3-LCD-EV-Board-SUB3, 4.3-inch 800x480 RGB LCD
- Touch: GT911/GT1151-compatible controller
- Flash: 16 MB
- Serial port: auto-detected when possible

## Build Environment

Use an ESP-IDF environment that supports ESP32-S31, then run commands from this
repository root.

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32s31
```

If `idf.py set-target` rewrites local configuration, that is fine. The actual
firmware apps are built by `rg_tool.py` with the S31 target settings.

## Build

Build the complete flash image:

```sh
idf.py build
```

The generated image is:

```text
retro-go-s31_unknown_esp32-s31-korvo-1.img
```

ESP-IDF also stages the latest image here for the default flash target:

```text
build/retro-go-s31-full.img
```

The version part changes when the repository is inside a Git checkout with tags.

## Flash

Auto-detect the serial port and flash:

```sh
idf.py flash
```

If auto-detection picks the wrong port, set it explicitly:

```sh
idf.py -p /dev/cu.usbserial-10 flash
```

You can also flash an existing image directly:

```sh
esptool.py --chip esp32s31 -p /dev/cu.usbserial-10 -b 1500000 \
  write_flash --flash_size detect 0x0 retro-go-s31_*.img
```

The ESP-IDF flash target uses a local esptool wrapper that enables the S31 stub
flasher and defaults to 1500000 baud. If your USB cable or adapter is unstable,
try `ESPBAUD=921600 idf.py flash`.

## Monitor

```sh
idf.py rg-monitor
```

For an explicit port:

```sh
RG_TOOL_PORT=/dev/cu.usbserial-10 idf.py rg-monitor
```

## Useful Targets

```sh
idf.py rg-build-apps   # build launcher, retro-core, gbsp without packing
idf.py rg-image        # build and pack the full image
idf.py rg-flash        # build, pack, and flash the full image
idf.py rg-monitor      # open the serial monitor
idf.py rg-clean        # remove app build outputs
idf.py build           # default ESP-IDF build, also builds the full image
idf.py flash           # default ESP-IDF flash, flashes the full image
```

## Built-In Game

This tree can embed a small NES ROM at:

```text
retro-core/main/builtin_game.nes
```

Only include ROMs you have the right to redistribute. For a public release,
prefer a homebrew/open-source ROM and document its license.

## Notes

- The root `main/` app is only a tiny placeholder so ESP-IDF recognizes the
  repository as a normal project. The default build/flash targets are redirected
  to the full Retro-Go image.
- The real firmware image contains three app partitions: `launcher`,
  `retro-core`, and `gbsp`.
- SD card mounting is optional for the built-in NES flow, but saves and external
  ROMs need a working SD card.
