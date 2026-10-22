#!/usr/bin/env bash
# One board's entry of ota-manifest.json (read by the LoRa OTA Android app), as out/<name>.ota.json.
#   tools/ota-manifest-entry.sh <env> <version> [elf]
# - file: the app image .bin the app makes a LoRa OTA delta from (repeaters only: pass the .elf)
# - dfu:  what the app flashes over Bluetooth (nRF52 DFU .zip) or over Wi-Fi (ESP32 app .bin)
# The release job merges them: jq -s '{files: .}' out/*.ota.json > out/ota-manifest.json
set -e
ENV=$1
VERSION=$2
ELF=$3

NAME=$(basename "$(ls out/*.uf2 out/*-merged.bin 2>/dev/null | head -1)")
NAME="${NAME%.uf2}"
NAME="${NAME%-merged.bin}"
[ -n "$NAME" ] || exit 0
# only nRF52 (has a DFU .zip) and ESP32 (has a -merged.bin) can be flashed or updated by the app
[ -f "out/${NAME}.zip" ] || [ -f "out/${NAME}-merged.bin" ] || exit 0

entry() {   # <key prefix> <file>: "file", "size", "sha256" (or "dfu", "dfu_size", "dfu_sha256")
  jq -n --arg k "$1" --arg f "$(basename "$2")" --arg s "$(sha256sum "$2" | cut -d' ' -f1)" \
        --argjson n "$(stat -c %s "$2")" \
        'if $k == "" then {file: $f, size: $n, sha256: $s} else {($k): $f, ($k + "_size"): $n, ($k + "_sha256"): $s} end'
}

parts=("$(jq -n --arg env "$ENV" --arg version "$VERSION" '{env: $env, version: $version}')")

if [ -n "$ELF" ] && [ -f "$ELF" ]; then
  if [ ! -f "out/${NAME}.bin" ]; then
    # nRF52: the app image is the .elf's loadable sections, as the repeater measures it
    OBJCOPY=$(find ~/.platformio/packages -name arm-none-eabi-objcopy -type f | head -1)
    "$OBJCOPY" -O binary "$ELF" "out/${NAME}.bin"
  fi
  parts+=("$(entry "" "out/${NAME}.bin")")
fi

if [ -f "out/${NAME}.zip" ]; then
  parts+=("$(entry dfu "out/${NAME}.zip")")
elif [ -f "out/${NAME}-merged.bin" ] && [ -f "out/${NAME}.bin" ]; then
  parts+=("$(entry dfu "out/${NAME}.bin")")
fi

[ ${#parts[@]} -gt 1 ] || exit 0
printf '%s\n' "${parts[@]}" | jq -s 'add' > "out/${NAME}.ota.json"
cat "out/${NAME}.ota.json"
