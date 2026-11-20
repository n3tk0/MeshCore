#!/usr/bin/env bash
# shellcheck shell=bash
# Build flags for a lighter firmware: everything not asked for is left out.
# Each WITH_* is "true" to keep that part as the board's platformio.ini has it; unset or anything
# else leaves it out (mailbox and LoRa OTA are kept unless set to "false").
#   WITH_RTC             external RTC chips (DS3231, RV3028, PCF8563): no I2C probes at boot
#   WITH_CLIMATE         temperature / humidity / pressure sensors (AHTX0, BME280, BMP280, SHTC3,
#                        SHT4X, LPS22HB, BME680 + BSEC, BMP085, MLX90614)
#   WITH_POWER_METERS    current / voltage meters (INA3221, INA219, INA226, INA260)
#   WITH_OTHER_SENSORS   VL53L0X distance, RAK12035 soil moisture
#   WITH_GPS             GPS module
#   WITH_DISPLAY         screen (without it the screen is a no-op; buttons, LED, buzzer still work)
#   WITH_MAILBOX         repeater mailbox for direct messages
#   WITH_LORA_OTA        repeater LoRa OTA
#   WITH_DEBUG           debug logs (MESH_DEBUG etc.)
# A board with a sensor built in (not a plug-in) says so in its env build_flags with one marker per sensor:
#   -D LITE_KEEP_INA260 -D LITE_KEEP_MLX90614
# (the macro is unused by the code, it only marks). Such a sensor is never undefined here and the I2C scan
# stays on. An env with -D ENV_INCLUDE_BQ25798 counts as having a built-in charger likewise.
# Source it before build.sh: it adds to PLATFORMIO_BUILD_FLAGS;
# the parts left out go one per line to the file $LITE_REMOVED, if set.
# LITE_ENV is the env being built (some boards cannot drop their GPS); run it from the repo root. Use a subshell, so the flags do not
# stay in your shell for later builds:
#   ( WITH_GPS=true LITE_ENV=Xiao_nrf52_repeater; source tools/lite-flags.sh; bash build.sh build-firmware $LITE_ENV )

flags=()
removed=()

keep() { [ "${!1}" = "true" ]; }
drop() { [ "${!1}" = "false" ]; }

# board-built-in sensors of the env (LITE_KEEP_<NAME> markers; BQ25798 from ENV_INCLUDE_BQ25798): never undefined
builtin=""
if [ -n "$LITE_ENV" ]; then
  builtin=$(pio project config --json-output 2>/dev/null | python3 -c '
import json, re, sys
for name, opts in json.load(sys.stdin):
    if name == "env:" + sys.argv[1]:
        t = json.dumps(opts)
        k = set(re.findall(r"LITE_KEEP_([A-Za-z0-9_]+)", t))
        if "ENV_INCLUDE_BQ25798" in t:
            k.add("BQ25798")
        print(" ".join(sorted(k)))
' "$LITE_ENV")
fi

undef() { local s; for s in "$@"; do case " $builtin " in *" $s "*) continue;; esac; flags+=("-UENV_INCLUDE_$s"); done; }

if ! keep WITH_RTC; then
  flags+=("-DDISABLE_RTC_AUTODISCOVERY")
  removed+=("RTC")
fi
if ! keep WITH_CLIMATE; then
  undef AHTX0 BME280 BMP280 SHTC3 SHT4X LPS22HB BME680 BME680_BSEC BMP085 MLX90614
  removed+=("climate sensors")
fi
if ! keep WITH_POWER_METERS; then
  undef INA3221 INA219 INA226 INA260
  removed+=("power meters")
fi
if ! keep WITH_OTHER_SENSORS; then
  undef VL53L0X RAK12035
  removed+=("other sensors")
fi
# a board with a built-in I2C part (the BQ25798 charger of the Xiao solar lite, an INA260 / MLX90614 of the MSPM0
# boards) needs the scan: without it the charger is never started and the battery reads from the (unconnected) ADC pin
# no I2C sensor kept: skip the bus scan as well (a stuck bus can hang the boot on nRF52)
if [ -n "$builtin" ]; then
  echo "I2C scan kept: the board has $builtin built in" >&2
elif ! keep WITH_CLIMATE && ! keep WITH_POWER_METERS && ! keep WITH_OTHER_SENSORS; then
  flags+=("-DDISABLE_ENV_SENSORS")
fi
if ! keep WITH_GPS; then
  # boards whose own sensor code starts a GPS with no ENV_INCLUDE_GPS check keep it: undefining it would
  # break the build (Heltec Tower v2, T-Deck) or leave the GPS running anyway (T1000-E, Tracker, ...)
  gps_kept=""
  if [ -n "$LITE_ENV" ]; then
    vdirs=$(pio project config --json-output 2>/dev/null | python3 -c '
import json, re, sys
for name, opts in json.load(sys.stdin):
    if name == "env:" + sys.argv[1]:
        text = json.dumps(opts)
        print("\n".join(sorted(set(re.findall(r"variants/([A-Za-z0-9_.-]+)", text)))))
' "$LITE_ENV")
    for v in $vdirs; do
      f=$(ls "variants/$v"/*.cpp "variants/$v"/*.h 2>/dev/null)
      [ -n "$f" ] || continue
      if grep -qE "LocationProvider|MicroNMEA" $f && ! grep -q "ENV_INCLUDE_GPS" $f; then
        gps_kept="$v"
      fi
    done
  fi
  if [ -n "$gps_kept" ]; then
    echo "GPS kept: variants/$gps_kept starts it in its own code" >&2
  else
    undef GPS
    removed+=("GPS")
  fi
  unset gps_kept vdirs v f
fi
if ! keep WITH_DISPLAY; then
  # the screen becomes a no-op (LiteNullDisplay); the UI task stays for the buttons, LED, buzzer and power off
  flags+=("-DLITE_NO_DISPLAY" "-include" "$PWD/src/helpers/ui/LiteNullDisplay.h")
  removed+=("display")
fi
if drop WITH_MAILBOX; then
  flags+=("-DNO_MAILBOX")
  removed+=("mailbox")
fi
if drop WITH_LORA_OTA; then
  flags+=("-DNO_LORA_OTA")
  removed+=("LoRa OTA")
fi
if ! keep WITH_DEBUG; then
  # the list of build.sh DISABLE_DEBUG=1, less -UCFG_DEBUG: the nRF52 core needs CFG_DEBUG defined
  flags+=(-UMESH_DEBUG -UMESH_PACKET_LOGGING -UBLE_DEBUG_LOGGING -UWIFI_DEBUG_LOGGING -UBRIDGE_DEBUG
          -UGPS_NMEA_DEBUG -UCORE_DEBUG_LEVEL -UESPNOW_DEBUG_LOGGING -UDEBUG_RP2040_WIRE -UDEBUG_RP2040_SPI
          -UDEBUG_RP2040_CORE -UDEBUG_RP2040_PORT -URADIOLIB_DEBUG_SPI -URADIOLIB_DEBUG_BASIC
          -URADIOLIB_DEBUG_PROTOCOL)
  removed+=("debug logs")
fi

if [ -n "$LITE_REMOVED" ]; then
  : > "$LITE_REMOVED"
  if [ ${#removed[@]} -gt 0 ]; then printf '%s\n' "${removed[@]}" > "$LITE_REMOVED"; fi
fi
export PLATFORMIO_BUILD_FLAGS="${PLATFORMIO_BUILD_FLAGS:+$PLATFORMIO_BUILD_FLAGS }${flags[*]}"
unset flags removed builtin
unset -f keep drop undef
