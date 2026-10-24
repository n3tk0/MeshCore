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

Every board OTAFIX has is built. What a repeater gets from it:

| | boards |
| --- | --- |
| staged install + rollback (QSPI flash, `-D QSPI_STORAGE` in the repeater env) | Xiao nRF52840 (also Ikoka Stick, Nano and Handheld, which use the Xiao bootloader), LilyGo T-Echo, ThinkNode M1, ThinkNode M6, Wio Tracker L1 (also L1 1W), SenseCAP Solar P1 |
| staged install, normal build (image up to 396 KB) | ProMicro / Faketec, T1000-E, ThinkNode M3, Heltec T114 without display, RAK WisMesh Tag, Minewsemi ME25LS01 |
| staged install with a light build only (`build-boards.yml`, about 310-336 KB) | RAK4631, RAK3401, Heltec T114 with display, Heltec T096, Heltec T1 |

Without QSPI the new image must fit next to the running one in the internal flash: up to 396 KB
(half the app area). A bigger one still updates over LoRa, but the old way (RAM copy, no power-cut
protection). Sizes per board: `notes` in the project, or build them.

The T-Echo Card and T-Echo Lite have no OTAFIX board of their own. RAK boards have at most 1 MB of
QSPI flash (too small for our layout). T114, T1000-E: the QSPI pins are not known yet, so no rollback.

To give another board rollback: its QSPI pins (`OTA_STAGE_QSPI_*`, nRF pin numbers) in its `board.h`
in `otafix.patch`, and `-D QSPI_STORAGE=1` in its repeater env. The flash must be at least 2 MB. If the
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
