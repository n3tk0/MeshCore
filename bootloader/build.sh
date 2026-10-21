#!/usr/bin/env bash
# Builds the OTAFIX bootloader with the LoRa OTA staged install (see README.md).
#   bootloader/build.sh [board ...]    default: every board OTAFIX has
# Output: bootloader/_out/<board>/ (update-*.uf2 to install over USB, *_nosd.hex, *_s140_*.hex)
set -euo pipefail

OTAFIX_REPO=https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX
OTAFIX_COMMIT=3a75482e17680169edd86760d122a154b1620c4d   # OTAFIX 2.3 (+ 2 commits)
OTA_VERSION=0.9.2-OTAFIX2.3-BP1.4-mcota1                          # shown in INFO_UF2.TXT

here=$(cd "$(dirname "$0")" && pwd)
work="$here/_work"
out="$here/_out"

if [ ! -d "$work/.git" ]; then
  git clone --quiet "$OTAFIX_REPO" "$work"
fi
cd "$work"
git fetch --quiet origin "$OTAFIX_COMMIT" 2>/dev/null || true
git checkout --quiet --force "$OTAFIX_COMMIT"
git clean --quiet -fdx
git submodule update --quiet --init --depth 1 lib/nrfx lib/tinyusb lib/uf2

git apply --whitespace=nowarn "$here/otafix.patch"
cp "$here/ota_stage.c" src/ota_stage.c
cp "$here/../src/helpers/nrf52/OtaStage.h" src/ota_stage.h   # one copy, shared with the app

BOARDS="$*"
[ -n "$BOARDS" ] || BOARDS=$(cd src/boards && ls -d */ | tr -d / | tr '\n' ' ')

rm -rf "$out"
for b in $BOARDS; do
  # -k: everything but the DFU .zip, which needs adafruit-nrfutil (install it to get that too)
  make BOARD="$b" GIT_VERSION="$OTA_VERSION" -j"$(nproc)" -k all >"$work/$b.log" 2>&1 || true
  ls "_build/build-$b/"update-*.uf2 >/dev/null 2>&1 || { tail -30 "$work/$b.log"; exit 1; }
  arm-none-eabi-size "_build/build-$b/"*.out
  mkdir -p "$out/$b"
  cp "_build/build-$b/"*.hex "_build/build-$b/"*.uf2 "$out/$b/"
  cp "_build/build-$b/"*.zip "$out/$b/" 2>/dev/null || true
done
ls -l "$out"/*
