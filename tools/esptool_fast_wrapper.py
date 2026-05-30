#!/usr/bin/env python3
import os
import sys

import esptool


def normalize_args(argv):
    args = list(argv)
    if args and args[0] == "esptool":
        args.pop(0)

    # ESP-IDF selects --no-stub for ESP32-S31, but the current esptool in this
    # environment can run the S31 stub flasher and is much faster with it.
    return [arg for arg in args if arg != "--no-stub"]


if __name__ == "__main__":
    sys.argv = [sys.argv[0], *normalize_args(sys.argv[1:])]
    os.environ.setdefault("ESPTOOL_ENV_FPGA", "0")
    esptool._main()
