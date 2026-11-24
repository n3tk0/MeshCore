#!/usr/bin/env bash
# Real screenshots of the Android app for the docs gallery (docs/android-app.bg.md).
#
# The images are rendered from the Compose code with Paparazzi (android/app/src/test/.../screens/ScreenshotsTest.kt),
# with made-up demo data, so they always match the app.
#
#   tools/android-screens.sh            record the screenshots, then copy them to docs/_assets/android_<name>.png
#   tools/android-screens.sh --copy     only copy what was recorded already
#   tools/android-screens.sh --out DIR  copy to DIR instead of docs/_assets (CI uses it for the artifact)
#
# Needs JDK 17+ and the Android SDK (ANDROID_HOME, or sdk.dir in android/local.properties).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
snap="$root/android/app/src/test/snapshots/images"
out="$root/docs/_assets"
record=1
while [ $# -gt 0 ]; do
  case "$1" in
    --copy) record=0 ;;
    --out) shift; out="$1" ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done

if [ "$record" -eq 1 ]; then
  (cd "$root/android" && ./gradlew --no-daemon :app:recordPaparazziDebug)
fi

mkdir -p "$out"
# test method in ScreenshotsTest -> docs/_assets/android_<name>.png
copy() {
  local src="$snap/io.github.n3tk0.loraota.screens_ScreenshotsTest_$1.png"
  if [ ! -f "$src" ]; then echo "missing: $src" >&2; exit 1; fi
  cp "$src" "$out/android_$2.png"
  echo "android_$2.png"
}
copy repeaterOverview repeater_overview
copy repeaterOverviewDark repeater_overview_dark
copy repeaterSettings repeater_settings_lora_calc
copy repeaterCli repeater_cli
copy offlineFirmware offline_firmware
copy offlineAudit offline_audit_mesh
copy fleetSweep fleet_sweep
copy startMyRepeaters start_my_repeaters
copy backupCompare backup_compare
copy profileBatch profile_batch
