#!/usr/bin/env bash
# Firmware size check, after the envs were built (CI or by hand):
#   tools/check-size.sh [--max-flash-pct <P>] [--log <file>] [--warn-only] <env> [<env>...]
# --log: the sizes from that build's output (one env) instead of `pio run -t checkprogsize`, which
# would build again when the build had other flags (build.sh adds the version, lite builds more).
# --warn-only: a warning, no failure (the PR check builds full upstream boards that only fit lite).
# Writes each env's image, flash and RAM use to the job summary ($GITHUB_STEP_SUMMARY, else stdout),
# and fails when
#  - an nRF52 repeater with LoRa OTA and no QSPI storage has an image over 396 KB: the bootloader's
#    staged copy in the internal flash has no room for it (bootloader/README.md); a lite build fits
#  - an env uses more than P % of its flash (--max-flash-pct, e.g. 95 for the Faketec companion,
#    whose dictionary leaves little room)

STAGED_MAX=$((396 * 1024))
max_pct=""
log=""
level=error
while [ $# -gt 1 ]; do
  case "$1" in
    --warn-only) level=warning; shift ;;
    --max-flash-pct) max_pct=$2; shift 2 ;;
    --log) log=$2; shift 2 ;;
    *) break ;;
  esac
done
[ $# -gt 0 ] || { echo "usage: $0 [--max-flash-pct P] [--log file] <env>..." >&2; exit 2; }

summary=${GITHUB_STEP_SUMMARY:-/dev/stdout}
fail=0
{
  echo "### Firmware size"
  echo
  echo "| Env | Image | Flash | RAM | Check |"
  echo "|---|---|---|---|---|"
} >> "$summary"

for env in "$@"; do
  if [ -n "$log" ]; then out=$(cat "$log"); else out=$(pio run -e "$env" -t checkprogsize 2>&1); fi
  # "Flash: [=======   ]  74.5% (used 531032 bytes from 712704 bytes)"
  flash=$(printf '%s\n' "$out" | sed -nE 's/^Flash: *\[[^]]*\] *(.*)$/\1/p' | tail -1)
  ram=$(printf '%s\n' "$out" | sed -nE 's/^RAM: *\[[^]]*\] *(.*)$/\1/p' | tail -1)
  pct=$(printf '%s\n' "$flash" | sed -nE 's/^ *([0-9.]+)%.*/\1/p')
  # the image: what the flash holds of it (code + initialised data), as the bootloader copies it
  size=$(printf '%s\n' "$flash" | sed -nE 's/.*used ([0-9]+) bytes.*/\1/p')
  size=${size:-0}

  # the defines as the env resolves them (extends included)
  staged=$(pio project metadata -e "$env" --json-output 2>/dev/null | python3 -c '
import json, sys
env = sys.argv[1]
try:
    d = json.load(sys.stdin)[env]["defines"]
except Exception:
    sys.exit()
names = {x.split("=")[0] for x in d}
print(1 if "repeater" in env.lower() and "NRF52_PLATFORM" in names
      and "QSPI_STORAGE" not in names and "NO_LORA_OTA" not in names else 0)
' "$env")

  check="ok"
  if [ "$staged" = "1" ] && [ "$size" -gt "$STAGED_MAX" ]; then
    check="**over 396 KB for the staged copy**"
    echo "::$level::$env: image $((size / 1024)) KB is over 396 KB, the staged OTA copy has no room (build it lite)"
    fail=1
  fi
  if [ -n "$max_pct" ] && [ -n "$pct" ] && awk "BEGIN { exit !($pct > $max_pct) }"; then
    check="**flash over $max_pct %**"
    echo "::$level::$env: flash $pct % is over $max_pct %"
    fail=1
  fi
  [ -n "$flash" ] || check="no size (not built?)"
  echo "| $env | $((size / 1024)) KB | ${flash:-?} | ${ram:-?} | $check |" >> "$summary"
done
[ "$level" = error ] || fail=0
exit $fail
