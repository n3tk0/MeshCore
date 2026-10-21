#!/bin/sh
# Host test of the bootloader's staged install (Linux: maps the nRF52840 flash at its real addresses)
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${TMPDIR:-/tmp}/test_ota_stage
cc -O1 -no-pie -Wall -Wextra -I "$here/fake" -o "$out" "$here/test_ota_stage.c" "$here/../ota_stage.c"
"$out"
