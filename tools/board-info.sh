#!/usr/bin/env bash
# One repeater board's description as JSON, made from its platformio.ini config plus the hand-written
# title / notes in docs/release-notes/boards.tsv:
#   tools/board-info.sh <env> [--oversize]
# {env, title, platform, radio, features: [...], description, description_bg}
# - platform: nRF52 | ESP32 | RP2040 | STM32; radio: the LoRa chip
# - features: what the build has (display, GPS, built-in sensors / chargers, LoRa OTA kind, bootloader support)
# - description / description_bg: one line (English / Bulgarian) of the above, then the board's note from boards.tsv
# - --oversize: the image is too big for the staged LoRa OTA copy (tools/check-size.sh failed): says so in the description
# boards.tsv: env<TAB>title<TAB>note_en<TAB>note_bg (lines starting with # are comments); an env with no row
# gets its env name as title. Run from anywhere; needs PlatformIO (BOARD_INFO_CONFIG=<file> uses a saved
# `pio project config --json-output` instead, to do many boards quickly). Never fails because of a missing
# config: the JSON then has only the title.
ENV=$1
[ -n "$ENV" ] || { echo "usage: $0 <env> [--oversize]" >&2; exit 2; }
OVERSIZE=0
[ "$2" = "--oversize" ] && OVERSIZE=1
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TSV=${BOARDS_TSV:-$ROOT/docs/release-notes/boards.tsv}

if [ -n "$BOARD_INFO_CONFIG" ] && [ -f "$BOARD_INFO_CONFIG" ]; then
  CONFIG=$(cat "$BOARD_INFO_CONFIG")
else
  CONFIG=$(cd "$ROOT" && pio project config --json-output 2>/dev/null) || CONFIG=""
fi

printf '%s' "$CONFIG" | BI_ENV=$ENV BI_ROOT=$ROOT BI_TSV=$TSV BI_OVERSIZE=$OVERSIZE python3 -I -c '
import json, os, re, sys

env = os.environ["BI_ENV"]
oversize = os.environ["BI_OVERSIZE"] == "1"

# hand-written part
title, note_en, note_bg = env, "", ""
try:
    with open(os.environ["BI_TSV"], encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            c = line.split("\t")
            if c[0] == env:
                title = c[1].strip() if len(c) > 1 and c[1].strip() else env
                note_en = c[2].strip() if len(c) > 2 else ""
                note_bg = c[3].strip() if len(c) > 3 else ""
                break
except OSError:
    pass

opts = None
try:
    for name, o in json.load(sys.stdin):
        if name == "env:" + env:
            opts = dict(o)
except Exception:
    pass

def finish(platform, radio, features, en, bg):
    if note_en: en.append(note_en)
    if note_bg: bg.append(note_bg)
    print(json.dumps({"env": env, "title": title, "platform": platform, "radio": radio, "features": features,
                      "description": " · ".join(en), "description_bg": " · ".join(bg)},
                     ensure_ascii=False))

if opts is None:
    finish("", "", [], [], [])
    sys.exit(0)

# -D / -U in order, as the compiler sees them: a later -U undoes an earlier -D
flags = " ".join(x for x in opts.get("build_flags", []) if isinstance(x, str))
d = {}
for m in re.finditer(r"(?<![A-Za-z0-9_])-([DU])\s*([A-Za-z0-9_]+)(?:=(\S*))?", flags):
    if m.group(1) == "D": d[m.group(2)] = m.group(3) if m.group(3) is not None else "1"
    else: d.pop(m.group(2), None)
def on(k): return k in d and d[k] not in ("0", "")

board = str(opts.get("board", "")).lower()
if on("NRF52_PLATFORM"): platform, mcu = "nRF52", "nRF52840"
elif on("ESP32_PLATFORM"):
    platform = "ESP32"
    # the board file says which chip (boards/<board>.json here, else the PlatformIO one); the name is the fallback
    jmcu = ""
    import glob
    for bf in [os.environ["BI_ROOT"] + "/boards/" + board + ".json"] + glob.glob(os.path.expanduser("~/.platformio/platforms/*/boards/" + board + ".json")):
        try:
            with open(bf, encoding="utf-8") as f:
                jmcu = str(json.load(f).get("build", {}).get("mcu", "")).lower()
        except Exception:
            continue
        if jmcu: break
    if jmcu in ("esp32c3", "esp32c6", "esp32s3", "esp32"): mcu = {"esp32c3": "ESP32-C3", "esp32c6": "ESP32-C6", "esp32s3": "ESP32-S3", "esp32": "ESP32"}[jmcu]
    elif re.search(r"esp32-?c3", board): mcu = "ESP32-C3"
    elif re.search(r"esp32-?c6", board): mcu = "ESP32-C6"
    elif re.search(r"s3|heltec_v4|t-deck|station-g2|t_beam_1w|heltec_e290|heltec_e213|heltec_tracker_v2", board): mcu = "ESP32-S3"
    else: mcu = "ESP32"
elif on("RP2040_PLATFORM"): platform, mcu = "RP2040", "RP2040"
elif on("STM32_PLATFORM"): platform, mcu = "STM32", "STM32WL"
else: platform, mcu = "", ""

RADIOS = {"CustomSX1262": "SX1262", "CustomSX1268": "SX1268", "CustomSX1276": "SX1276", "CustomLR1110": "LR1110",
          "CustomLR1121": "LR1121", "CustomLR2021": "LR2021", "CustomSTM32WLx": "STM32WL built-in (SX126x)"}
radio = RADIOS.get(d.get("RADIO_CLASS", ""), d.get("RADIO_CLASS", "").replace("Custom", ""))

features = []
en = [mcu] if mcu else []
bg = list(en)
if platform == "STM32":
    en[:] = ["STM32WL (built-in SX126x radio)"]; bg[:] = list(en)
elif radio:
    en.append(radio); bg.append(radio)

# display
dc = d.get("DISPLAY_CLASS", "")
if dc and dc != "NullDisplayDriver":
    if re.search(r"GxEPD|E213|E290", dc): kind, kind_bg = "e-paper", "e-paper"
    elif re.search(r"SSD1306|SH1106|U8g2", dc): kind, kind_bg = "OLED", "OLED"
    elif re.search(r"ST77|NV3001", dc): kind, kind_bg = "TFT", "TFT"
    else: kind, kind_bg = "", ""
    features.append((kind + " display").strip())
    en.append((kind + " display").strip()); bg.append(("дисплей " + kind_bg).strip())
else:
    features.append("no display"); en.append("no display"); bg.append("без дисплей")

if on("ENV_INCLUDE_GPS"):
    features.append("GPS"); en.append("GPS"); bg.append("GPS")

# built-in parts the board marks itself (see tools/lite-flags.sh)
extra_en, extra_bg = [], []
if on("SOLAR_BOARD_AUTO"):
    extra_en.append("solar charger auto-detect (BQ25798 / CN3791 / MSPM0)"); extra_bg.append("автоматично разпознаване на соларно зарядно (BQ25798 / CN3791 / MSPM0)")
elif on("ENV_INCLUDE_BQ25798"):
    extra_en.append("BQ25798 solar charger"); extra_bg.append("BQ25798 соларно зарядно")
if on("INA226_BATTERY"): extra_en.append("battery from INA226"); extra_bg.append("батерия през INA226")
kept = [k[len("LITE_KEEP_"):] for k in sorted(d) if k.startswith("LITE_KEEP_") and on(k)]
if kept:
    extra_en.append("built-in " + "/".join(kept)); extra_bg.append("вградени " + "/".join(kept))
features += extra_en
en += extra_en; bg += extra_bg

# mailbox, LoRa OTA and the bootloader
if "NO_MAILBOX" not in d: features.append("mailbox")
lora_ota = "NO_LORA_OTA" not in d and platform in ("nRF52", "ESP32")
STAGED = ("ProMicro_repeater", "t1000e_repeater", "ThinkNode_M3_repeater", "Heltec_t114_without_display_repeater",
          "RAK_WisMesh_Tag_repeater", "Minewsemi_me25ls01_repeater")
STAGED_LITE = ("RAK_4631_repeater", "RAK_3401_repeater", "Heltec_t114_repeater", "Heltec_t096_repeater", "Heltec_t1_repeater")
if lora_ota:
    features.append("LoRa OTA")
    if platform == "nRF52":
        if on("QSPI_STORAGE"):
            pass
        elif oversize:
            # tools/check-size.sh: bigger than the staged copy can take, whatever the board
            features.append("too big for staged OTA")
            en.append("LoRa OTA the old way (RAM copy, no power-cut protection): this full build is too big for the staged install, a Bluetooth/USB update is safer")
            bg.append("LoRa OTA по стария начин (копие през RAM, без защита от спиране на тока): този пълен build е твърде голям за staged инсталация, по-сигурно е обновяване през Bluetooth/USB")
        if on("QSPI_STORAGE"):
            features.append("rollback (QSPI)")
            en.append("dual-image LoRa OTA with rollback (QSPI)"); bg.append("LoRa OTA с два образа и rollback (QSPI)")
        elif oversize:
            pass
        elif env in STAGED:
            features.append("staged install")
            en.append("LoRa OTA, power-cut safe staged install with our bootloader (image up to 396 KB)")
            bg.append("LoRa OTA, staged инсталация през нашия bootloader, безопасна при спиране на тока (образ до 396 KB)")
        elif env in STAGED_LITE:
            en.append("LoRa OTA, staged install with our bootloader needs a lite build (this full build copies the old way)")
            bg.append("LoRa OTA, staged инсталация през нашия bootloader само с lite build (този пълен build копира по стария начин)")
        else:
            en.append("LoRa OTA (old way: RAM copy, no power-cut protection)")
            bg.append("LoRa OTA (по стария начин: копие през RAM, без защита от спиране на тока)")
    else:
        en.append("LoRa OTA (two app slots)"); bg.append("LoRa OTA (два слота за приложението)")
else:
    en.append("no LoRa OTA"); bg.append("без LoRa OTA")

finish(platform, radio, features, en, bg)
'
