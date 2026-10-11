#!/bin/sh
# Builds and runs the screen simulator; PNGs and manifest.tsv go to tools/ui-sim/out (or $1).
set -e
cd "$(dirname "$0")"
ROOT=../..
OUT=${1:-out}
mkdir -p "$OUT"
rm -f "$OUT"/*.png "$OUT"/manifest.tsv
c++ -std=c++17 -O1 -funsigned-char -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable \
  -DUI_SIM -DUI_HAS_ROTARY_INPUT -DPIN_ENCODER_BTN=1 -DPIN_USER_BTN=2 \
  -DMAX_CONTACTS=160 -DMAX_GROUP_CHANNELS=40 -DUI_ASK_ANTENNA -DAUTO_SHUTDOWN_MILLIVOLTS=3300 \
  -Istubs -I. -I$ROOT/src -I$ROOT/examples/companion_radio/ui-encoder \
  sim_main.cpp \
  $ROOT/examples/companion_radio/ui-encoder/UITask.cpp \
  $ROOT/examples/companion_radio/ui-encoder/WordPredictor.cpp \
  -o ui-sim
./ui-sim "$OUT"
