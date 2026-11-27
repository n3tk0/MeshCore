# nRF52 bootloader for LoRa OTA

The [OTAFIX bootloader](https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX) with one addition:
a **staged install** for LoRa OTA updates of our repeaters. Only our changes live here; `build.sh`
fetches OTAFIX at a fixed commit, applies them and builds.

| file | what |
| --- | --- |
| `ota_stage.c` | the staged install, run at every start before the DFU checks |
| `../src/helpers/nrf52/OtaStage.h` | the record format, shared with the firmware (copied in as `src/ota_stage.h`) |
| `otafix.patch` | the hook in `main.c`, the build, the feature marker in the linker script, QSPI pins per board |
| `test/` | host test of `ota_stage.c`: install, rollback, power cuts at every step |

## What it changes

Without it, the repeater copies the new image over itself from RAM (~20 s). A power cut then leaves
no app: the node waits in BLE DFU until someone goes there.

With it, the repeater puts the whole new image where the copy does not touch it, writes a small record
in the bootloader settings page and resets. The bootloader checks the image (CRC-32), copies it, checks
the result and marks the record. The source is never written, so after a power cut the copy just
starts again.

On boards with a QSPI flash (see below) the new image comes from there, and the running image is kept
there too (0x130000; both get 832 KB, more than the app area). The new image then runs **on trial**: the firmware confirms it when an admin logs in, the
LoRa OTA app asks for INFO, or after 10 minutes. If it restarts 3 times without that (a crash, or a hang
with the 60 s watchdog), the bootloader copies the old image back. `ota rollback` on the repeater does
the same by hand.

Firmware with an old bootloader still updates the old way (and now also stores the image's CRC16, which
the stock bootloader checks at every start).

## Boards

### Why only some boards get our bootloader

A bootloader is not one file for every nRF52 board: it knows the board's USB, LEDs, buttons and the
name it shows in `INFO_UF2.TXT`. A wrong one can leave a board that no longer enters its UF2 / DFU
mode, and then only a SWD programmer brings it back. So we build ours only for the boards that
OTAFIX itself has a board for, and only change what the LoRa OTA needs. Every other board keeps the
bootloader it came with. What decides what a board gets:

1. **An OTAFIX board for it.** OTAFIX has 17 nRF52840 boards (`src/boards/` at the pinned commit). A
   board without one (Heltec Mesh Solar, Tower V2, Mesh Pocket, ThinkNode M4 / M8, T-Echo Card / Lite,
   GAT562, R1 Neo, ...) stays on its stock bootloader, even when it is close to a supported one.
2. **Room for the new image.** The bootloader copies the new image from a place the copy does not
   touch. Without a QSPI flash that is the internal flash after the running image, so the image may
   be at most 396 KB (half the app area). A bigger full build still updates over LoRa, the old way;
   a light build (`build-boards.yml`, about 310-336 KB) fits.
3. **A QSPI flash for rollback.** Keeping the old image as well needs an external flash of at least
   2 MB whose pins are known (`-D QSPI_STORAGE` in the repeater env). RAK boards have at most 1 MB;
   on the T114 and T1000-E the pins (and on the T1000-E its power pin) are not confirmed yet.
4. **Not needed on ESP32.** The ESP32 already has two app partitions: a new image goes into the other
   one and runs on trial, and its own bootloader goes back if it is not confirmed. Nothing to install.
5. **No LoRa OTA on RP2040 and STM32.** Those repeaters are updated by cable, so a bootloader would
   change nothing.

Installing our bootloader takes a USB cable once per node (or the board's own DFU). After that every
update can go over LoRa. A node that stays on its stock bootloader still updates over LoRa, but the
old way: a power cut in the ~20 s of the final copy leaves it in the bootloader (not broken, but it
needs a visit with USB or Bluetooth DFU).

### Every repeater board

"Ours" means `update-<file>_bootloader-0.9.2-OTAFIX2.3-BP1.4-mcota1_nosd.uf2` from a `bootloader-v*`
release (or the Build Bootloader artifacts). Use it only when `INFO_UF2.TXT` on the board's drive names
the same board; if it names another, keep the stock one.

**nRF52 with our bootloader**

| Board | Repeater env(s) | Bootloader file | LoRa OTA gets |
| --- | --- | --- | --- |
| Seeed XIAO nRF52840 + Wio-SX1262 | `Xiao_nrf52_repeater`, `Xiao_nrf52_solar_repeater`, `Xiao_nrf52_solar_lite_repeater` | `xiao_nrf52840_ble` (`xiao_nrf52840_ble_sense` only if `INFO_UF2.TXT` says `SeeedXiaoSense`) | staged + rollback |
| Ikoka Stick / Nano / Handheld nRF | `ikoka_stick_nrf_*`, `ikoka_nano_nrf_*`, `ikoka_handheld_nrf_e22_30dbm_repeater` | `xiao_nrf52840_ble` (XIAO inside) | staged + rollback |
| Seeed SenseCAP Solar P1 | `SenseCap_Solar_repeater` | `sensecap_solar_p1` | staged + rollback |
| LilyGo T-Echo | `LilyGo_T-Echo_repeater` | `lilygo_techo` | staged + rollback |
| Elecrow ThinkNode M1 | `ThinkNode_M1_repeater` | `thinknode_m1` | staged + rollback |
| Elecrow ThinkNode M6 | `ThinkNode_M6_repeater` | `thinknode_m6` | staged + rollback |
| Seeed Wio Tracker L1 / L1 1W | `WioTrackerL1_repeater`, `WioTrackerL1-1W_repeater` | `wio_tracker_l1` | staged + rollback |
| Pro Micro nRF52840 / Faketec | `ProMicro_repeater` | `promicro_nrf52840` | staged |
| Seeed SenseCAP T1000-E | `t1000e_repeater` | `t1000_e` | staged |
| Elecrow ThinkNode M3 | `ThinkNode_M3_repeater` | `thinknode_m3` | staged |
| RAK WisMesh Tag | `RAK_WisMesh_Tag_repeater` | `wismesh_tag` | staged (387 KB, close to the limit) |
| Minewsemi ME25LS01 | `Minewsemi_me25ls01_repeater` | `minewsemi_mx25le01` | staged |
| Heltec Mesh Node T114 | `Heltec_t114_without_display_repeater`, `Heltec_t114_repeater` | `heltec_t114` | staged; with display only as a light build |
| RAKwireless RAK4631 | `RAK_4631_repeater` | `wiscore_rak4631_board` | staged only as a light build (full: 459 KB) |
| RAKwireless RAK3401 | `RAK_3401_repeater` | `wiscore_rak3401` | staged only as a light build (full: 429 KB) |
| Heltec Mesh Node T096 | `Heltec_t096_repeater` | `heltec_t096` | staged only as a light build (full: 432 KB) |
| Heltec Mesh Node T1 | `Heltec_t1_repeater` | `heltec_t1` | staged only as a light build (full: 431 KB) |

"staged": the final copy survives a power cut. "rollback": also goes back to the old image if the new
one does not come up. A full build over 396 KB on a board without rollback updates the old way even
with our bootloader. Sizes are of the 2026-10-21 builds; CI's size check (`tools/check-size.sh`) has the
current ones.

**nRF52 on the stock bootloader** (no OTAFIX board): LoRa OTA the old way, a power cut in the final
copy leaves the node in the bootloader.

| Board | Repeater env(s) | Note |
| --- | --- | --- |
| GAT562 30S Mesh Kit / Mesh EVB Pro / Mesh Tracker Pro | `GAT562_*_repeater` | RAK4631 clone; the RAK4631 file is not checked on it |
| Muzi Works R1 Neo | `R1Neo_repeater` | RAK4631 layout; not checked |
| Muzi Works Base Duo / Uno | `muzi_base_duo_repeater`, `muzi_base_uno_repeater` | |
| Heltec Mesh Solar | `Heltec_mesh_solar_repeater` | |
| Heltec Mesh Tower V2 | `Heltec_tower_v2_repeater` | |
| Heltec Mesh Pocket | `Mesh_pocket_repeater` | |
| LilyGo T-Echo Card | `LilyGo_T-Echo_Card_repeater` | has a 4 MB QSPI flash, but no OTAFIX board |
| LilyGo T-Echo Lite | `LilyGo_T-Echo-Lite_repeater` | |
| LilyGo T-Impulse Plus | `LilyGo_T_Impulse_Plus_repeater` | |
| Elecrow ThinkNode M4 | `ThinkNode_M4_repeater` | |
| Elecrow ThinkNode M8 | `ThinkNode_M8_repeater` | |
| Keepteen LT1 | `KeepteenLT1_repeater` | |
| Seeed Mesh Tracker X1 | `MeshTracker_X1_repeater` | |
| Seeed Wio-WM1110 | `wio_wm1110_repeater` | |
| B&Q Nano G2 Ultra | `Nano_G2_Ultra_repeater` | |

**ESP32 (stock, nothing to install):** LoRa OTA into the other app partition, on trial, with rollback.
Ebyte EoRa-S3, Generic E22 (SX1262 / SX1268), Heltec CT62, Vision Master E213 / E290, RC32 (with and
without display), Wireless Tracker (V1, V2), WiFi LoRa 32 V2 / V3 / V4 (all V4 variants), Wireless Paper,
Wireless Stick Lite V3, LilyGo T3-S3 (SX1262 / SX1276), T-Beam (SX1262 / SX1276), T-Beam 1W, T-Beam S3
Supreme, T-Deck, T-ETH Elite, T-LoRa V2.1-1.6, M5Stack Unit C6L, Meshadventurer (SX1262 / SX1268),
Meshnology W12, RAK3112, B&Q Station G2 / G3 (also logging), Tenstar C3 (SX1262 / SX1268), ThinkNode M2 /
M5 / M7, Waveshare S3 LR1121, Seeed XIAO ESP32-C3 / S3 / S3 + Wio.

**RP2040 and STM32 (stock, no LoRa OTA, update by cable):** Raspberry Pi Pico W, RAK11310, Waveshare
RP2040-LoRa, Seeed XIAO RP2040; RAK3172 (RAK_3x72), Tiny Relay, Seeed Wio-E5 mini and dev board.

### Adding a board

A board needs its own folder in OTAFIX `src/boards/` (then `build.sh` builds it with no change). To
give it rollback: its QSPI pins (`OTA_STAGE_QSPI_*`, nRF pin numbers) in its `board.h` in
`otafix.patch`, and `-D QSPI_STORAGE=1` in its repeater env. The flash must be at least 2 MB. If the
app switches the flash's supply on (a power-enable pin), name that pin as `OTA_STAGE_QSPI_PWR_1` (and
`_2`): after a reset the pins float, so the bootloader drives them high itself (ThinkNode M6: P0.27
and P0.21; T-Echo, ThinkNode M1: P0.12).

## Build

```
bootloader/build.sh                      # every board
bootloader/build.sh xiao_nrf52840_ble    # or the boards you name
```

Needs `arm-none-eabi-gcc` and `pip install intelhex`. Output: `bootloader/_out/<board>/`. CI:
`.github/workflows/build-bootloader.yml` (tag `bootloader-v*` makes a release).

## Install

Double-tap RST, copy `update-<board>_bootloader-…-mcota1_nosd.uf2` to the drive. Use the file for your
board (`INFO_UF2.TXT` on the drive names the board the bootloader was built for; on a Xiao use Sense
only if it says `SeeedXiaoSense`).

The update writes the new bootloader into the top of the app area first (as stock OTAFIX does). That is
free on a repeater; on a companion with ExtraFS do the erase that the OTAFIX README asks for.

## Layout (nRF52840)

| where | what |
| --- | --- |
| `0xFD7F8` | `"MOT1"` + features (1 staged install, 2 QSPI + rollback): the firmware reads it |
| `0xFF000` | bootloader settings (28 bytes), then from `0x400` 24 record slots of 128 bytes |
| QSPI `0x000000` | the new image (repeaters with `QSPI_STORAGE`) |
| QSPI `0x0D0000` | the repeater's mailbox (24 slots of 16 KB) |
| QSPI `0x130000` | the image that ran before (rollback) |

A normal DFU (UF2, BLE) erases the settings page and with it every record.
