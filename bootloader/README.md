# nRF52 bootloader for LoRa OTA

The [OTAFIX bootloader](https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX) with one addition:
a **staged install** for LoRa OTA updates of our repeaters. Only our changes live here; `build.sh`
fetches OTAFIX at a fixed commit, applies them and builds.

| file | what |
| --- | --- |
| `ota_stage.c` | the staged install, run at every start before the DFU checks |
| `../src/helpers/nrf52/OtaStage.h` | the record format, shared with the firmware (copied in as `src/ota_stage.h`) |
| `otafix.patch` | the hook in `main.c`, the build, the feature marker in the linker script, Xiao QSPI pins |
| `test/` | host test of `ota_stage.c`: install, rollback, power cuts at every step |

## What it changes

Without it, the repeater copies the new image over itself from RAM (~20 s). A power cut then leaves
no app: the node waits in BLE DFU until someone goes there.

With it, the repeater puts the whole new image where the copy does not touch it, writes a small record
in the bootloader settings page and resets. The bootloader checks the image (CRC-32), copies it, checks
the result and marks the record. The source is never written, so after a power cut the copy just
starts again.

On the Xiao nRF52840 the new image comes from the QSPI flash, and the running image is kept there too
(0x180000). The new image then runs **on trial**: the firmware confirms it when an admin logs in, the
LoRa OTA app asks for INFO, or after 10 minutes. If it restarts 3 times without that (a crash, or a hang
with the 60 s watchdog), the bootloader copies the old image back. `ota rollback` on the repeater does
the same by hand.

Firmware with an old bootloader still updates the old way (and now also stores the image's CRC16, which
the stock bootloader checks at every start).

## Build

```
bootloader/build.sh                      # ProMicro, Xiao nRF52840, Xiao nRF52840 Sense
bootloader/build.sh xiao_nrf52840_ble    # or the boards you name
```

Needs `arm-none-eabi-gcc` and `pip install intelhex`. Output: `bootloader/_out/<board>/`. CI:
`.github/workflows/build-bootloader.yml` (tag `bootloader-v*` makes a release).

## Install

Double-tap RST, copy `update-<board>_bootloader-…-mcota1_nosd.uf2` to the drive. Use the file for your
board; on a Xiao check `INFO_UF2.TXT` (Sense only if it says `SeeedXiaoSense`).

The update writes the new bootloader into the top of the app area first (as stock OTAFIX does). That is
free on a repeater; on a companion with ExtraFS do the erase that the OTAFIX README asks for.

## Layout (nRF52840)

| where | what |
| --- | --- |
| `0xFD7F8` | `"MOT1"` + features (1 staged install, 2 QSPI + rollback): the firmware reads it |
| `0xFF000` | bootloader settings (28 bytes), then from `0x400` 24 record slots of 128 bytes |
| QSPI `0x000000` | the new image (Xiao repeater) |
| QSPI `0x180000` | the image that ran before (rollback) |

A normal DFU (UF2, BLE) erases the settings page and with it every record.
