# Repeater firmware of the n3tk0 fork / Repeater фърмуер от форка на n3tk0

Пълни (не lite) repeater фърмуери за **всички платки**, с mailbox и LoRa OTA (където платформата го поддържа), без debug логове. Full repeater builds for **all boards**, with the mailbox and LoRa OTA (where the platform supports it), no debug logs. Every board is the build of its own `platformio.ini` env; the table at the end of this page lists each one.

> Отделна версия на [MeshCore](https://github.com/meshcore-dev/MeshCore) (upstream: meshcore-dev/MeshCore). / A fork of upstream MeshCore.

---

## Български

### Какво добавя този форк към repeater-ите

Mailbox за лични съобщения, LoRa OTA обновяване от Android приложението, наш nRF52 bootloader с безопасна инсталация и rollback, по-здрава I2C шина, батерия от INA219/INA226, соларни платки (BQ25798, CN3791), стъпки за пестене на батерия, self test, история на телеметрията и поправки към upstream. Подробното описание е по-долу на английски (взето от README на репото, за да е винаги актуално).

### Как се инсталира

| Платформа | Първа инсталация | Обновяване |
|---|---|---|
| **nRF52** | `.uf2` на bootloader устройството: два бързи натиска на RST, копирай файла на появилото се устройство | LoRa OTA от приложението (`.bin` в `ota-manifest.json`) или Bluetooth DFU от приложението с `.zip` |
| **ESP32** | `...-merged.bin` на адрес **0x0** (напр. с MeshCore Flasher) | LoRa OTA от приложението, или през Wi-Fi с `.bin` (app образ) |
| **RP2040** | `.uf2` (или `.bin`) | презаписване (няма LoRa OTA) |
| **STM32** | `.bin` / `.hex` | презаписване (няма LoRa OTA) |

За да работи staged инсталацията и rollback-ът на nRF52, **поставете нашия bootloader веднъж** (`update-<платка>_bootloader-...-mcota1_nosd.uf2` от издание `bootloader-v*`). Без него обновяването по LoRa става по стария начин (копие през RAM, без защита от спиране на тока). В `SHA256SUMS` са контролните суми на всички файлове (`sha256sum -c SHA256SUMS`).

### Как да намерите вашата платка

Вижте таблицата "Boards in this release" по-долу: намерете платката по име (колона Board) или по името на env (колона Env). Файловете в това издание започват с името на env: `<env>-<версия>-<commit>.uf2` / `.zip` / `.bin` / `-merged.bin`. Описанието казва чипа, радиото, дисплея, GPS-а и какъв вид LoRa OTA има платката. Приложението "LoRa OTA" чете същото описание от `ota-manifest.json` и го показва при избора на фърмуер. Ако платката ви не е в списъка, не е компилирана в тази версия (виж "Not built").

---

## English

### How to install

| Platform | First install | Update |
|---|---|---|
| **nRF52** | the `.uf2` on the bootloader drive: double-tap RST and copy the file to the drive that appears | LoRa OTA from the app (the `.bin` listed in `ota-manifest.json`) or Bluetooth DFU from the app with the `.zip` |
| **ESP32** | `...-merged.bin` at address **0x0** (e.g. with the MeshCore Flasher) | LoRa OTA from the app, or over Wi-Fi with the `.bin` (app image) |
| **RP2040** | the `.uf2` (or the `.bin`) | flash again (no LoRa OTA) |
| **STM32** | the `.bin` / `.hex` | flash again (no LoRa OTA) |

For the staged install and rollback on nRF52, **put our bootloader on the board once** (`update-<board>_bootloader-...-mcota1_nosd.uf2` from a `bootloader-v*` release). Without it a LoRa OTA update copies the old way (through RAM, no power-cut protection). `SHA256SUMS` has the checksums of every file (`sha256sum -c SHA256SUMS`).

### How to find your board

See the "Boards in this release" table below: find your board by name (column Board) or by env name (column Env). The files in this release start with the env name: `<env>-<version>-<commit>.uf2` / `.zip` / `.bin` / `-merged.bin`. The description names the chip, the radio, the display, GPS and the kind of LoRa OTA the board has. The "LoRa OTA" app reads the same description from `ota-manifest.json` and shows it when you pick a firmware. A board that is not in the table was not built in this run (see "Not built").

### What this fork adds to repeaters

<!-- README:repeater -->
