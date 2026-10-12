#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the release firmware (diagnostics off) and write the two files a release carries:
#   blue-baton-firmware-<version>-esp32s3.bin      whole image at 0x0: first install (also clears pairings)
#   blue-baton-firmware-<version>-esp32s3-app.bin  app only at 0x20000: update that keeps pairings
#   firmware/tools/package.sh v0.7.1 dist
# release.yml runs it in the espressif/idf:v6.1 container; locally it needs ESP-IDF v6.1 in the environment.
set -e
[ $# -eq 2 ] || { echo "usage: $0 <version> <output dir>"; exit 1; }
version="$1"
mkdir -p "$2"
out="$(cd "$2" && pwd)"
cd "$(dirname "$0")/.."
idf.py build
cd build
python -m esptool --chip esp32s3 merge-bin -o "$out/blue-baton-firmware-$version-esp32s3.bin" @flash_args
cp blue_baton.bin "$out/blue-baton-firmware-$version-esp32s3-app.bin"
ls -l "$out"
