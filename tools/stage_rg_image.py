#!/usr/bin/env python3
import argparse
import glob
import os
import shutil
import sys


def main():
    parser = argparse.ArgumentParser(description="Stage the latest Retro-Go image for ESP-IDF flash targets")
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    pattern = os.path.join(args.source_dir, f"retro-go-s31_*_{args.target}.img")
    images = [path for path in glob.glob(pattern) if os.path.isfile(path)]
    if not images:
        sys.exit(f"No Retro-Go image found matching: {pattern}")

    image = max(images, key=os.path.getmtime)
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    shutil.copyfile(image, args.output)
    print(f"Staged Retro-Go image: {image} -> {args.output}")


if __name__ == "__main__":
    main()
