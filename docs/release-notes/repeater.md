# Repeater firmware of the n3tk0 fork / Repeater фърмуер от форка на n3tk0

Пълни (не lite) repeater фърмуери за **всички платки**, с mailbox и LoRa OTA (където платформата го поддържа), без debug логове. Full repeater builds for **all boards**, with the mailbox and LoRa OTA (where the platform supports it), no debug logs. Every board is the build of its own `platformio.ini` env; the table at the end of this page lists each one.

> Отделна версия на [MeshCore](https://github.com/meshcore-dev/MeshCore) (upstream: meshcore-dev/MeshCore). / A fork of upstream MeshCore.

---

## Български

### Какво добавя този форк към repeater-ите

- **Mailbox (store and forward) за лични съобщения.** Repeater-ът пази до 4 собственика-companion лични съобщения, докато ги няма, и ги предава, когато собственикът бъде чут без hop между тях. Нищо не се залива в мрежата: съобщенията се пазят от трафик, който repeater-ът чува така или иначе, и се предават като zero-hop пакети. Пазят се само `TXT_MSG` към собственик, все още криптирани (repeater-ът не ги чете). До 64 съобщения (8 на STM32) във флаш паметта. Изключен е, докато не се пусне `mailbox learn` или `mailbox on`; `-D NO_MAILBOX` го маха от build.
- **LoRa OTA обновяване.** Android приложението "LoRa OTA" (`android/`) прави delta от фърмуера, който repeater-ът върти, към новия и я праща през companion-а, само без hop, по странично LoRa пространство, така че основният канал продължава да работи. Изключено е по подразбиране: в конзолата на repeater-а `set ota on`. Приложението ползва 10 % от всеки час въздушно време. Работи на nRF52 и ESP32.
- **Наш nRF52 bootloader** (на основата на OTAFIX) със **staged инсталация, безопасна при спиране на тока**: новият образ се записва там, където копирането не го засяга, bootloader-ът го проверява (CRC-32), копира го и проверява резултата. При платки с QSPI флаш (напр. Xiao nRF52840) има и **rollback**: новият образ върви "на проба" и ако 3 пъти рестартира без потвърждение, старият се връща. Новият фърмуер трябва да докаже радиото си (вход като admin, или изпратен и приет пакет в рамките на 10 минути). Bootloader-ът се инсталира веднъж за всяка платка (виж `bootloader/README.md`).
- **По-здрава I2C шина на nRF52:** собствен `nrf52_wire` драйвер с таймаут 50 ms на всяко чакане, нулиране на TWIM и възстановяване на шината. Закачен кабел или модул, държащ линията, вече не спира възела при стартиране. Устройство, което не отговаря, се изключва до рестарт, а останалите продължават (`i2c` показва какво е открито и какво е изключено). Сензор, изключен заради грешки, се опитва пак след 10 минути, 1 час, после на всеки 6 часа.
- **Батерия от INA219 / INA226:** INA219 в линията на батерията дава напрежение и ток (със знак) вместо ADC на платката; с `-D INA226_BATTERY=1` същото за INA226. Броене на заряда: реален %, часове до край, mAh днес и вчера.
- **Соларни платки:** BQ25798 (xiao-solar-lite v0.5, xiao-solar) и CN3791 с два INA226 (xiao-solar-lite v0.6). `Xiao_nrf52_solar_repeater` е един фърмуер за всички XIAO соларни платки и ги разпознава при стартиране (команда `solar`, `charger`). Подробности: `docs/solar-boards.bg.md`.
- **`battsave` - стъпки за пестене на батерия:** под 3.6 V adverts-ите са наполовина, под 3.45 V няма LoRa OTA и mailbox; стъпката "изключване" не е включена по подразбиране (`battsave 3600 3450 3300`).
- **`selftest`** - един ред за приложението (страница Health) и за хора. **История на телеметрията** 24 часа през 10 минути и седмица през час (не на STM32).
- **Packet filter и block list** (изключени по подразбиране): `filter ...` в CLI-то, виж `docs/packet_filter.md` (не влиза в STM32 build-овете).
- **Поправки към upstream** в общия код: срив при къси кадри от приложението, подправени радио пакети, атомарни записи на nRF52 (контакти, канали, настройки), състезание в BLE RX опашката на nRF52 и др.

> Състояние: mailbox, LoRa OTA и bootloader-ът минават unit и host тестове и се компилират в CI, но още не са изпитани на хардуер; същото важи за battsave, selftest, историята на телеметрията и страниците Health в приложението. Соларните платки са "not hardware-tested yet".

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

### What this fork adds to repeaters

- **Mailbox (store and forward) for direct messages.** The repeater keeps direct messages for up to 4 owner companions while they are away and hands them over when the owner is heard again with no hop between. Nothing is flooded: messages are kept from traffic the repeater hears anyway and handed over as zero-hop packets. Only `TXT_MSG` addressed to an owner, still encrypted (the repeater cannot read them), up to 64 messages (8 on STM32) in flash. It is off until `mailbox learn` or `mailbox on`; `-D NO_MAILBOX` leaves it out of a build.
- **LoRa OTA updates.** The "LoRa OTA" Android app (`android/`) makes a delta from the firmware the repeater runs to the new one and sends it through your companion, only with no hop, on a side channel so the main channel keeps working. Off by default: run `set ota on` in the repeater's console. The app keeps to 10 % of each hour of airtime. nRF52 and ESP32.
- **Our nRF52 bootloader** (based on OTAFIX) with a **power-cut safe staged install**: the new image is put where the copy does not touch it, the bootloader checks it (CRC-32), copies it and checks the result. Boards with QSPI flash (e.g. Xiao nRF52840) also get **rollback**: the new image runs on trial and after 3 restarts without confirmation the old one is copied back. New firmware must prove its radio (admin login, or a packet sent and received within 10 minutes). Install the bootloader once per board (see `bootloader/README.md`).
- **A sturdier I2C bus on nRF52:** our own `nrf52_wire` driver with a 50 ms timeout on every wait, a TWIM reset and bus recovery. A loose cable or a module holding a line no longer stops a node at boot; a device that does not answer is turned off until a reboot and the others go on (`i2c` lists what was found and what was turned off). A sensor turned off for errors is tried again after 10 minutes, 1 hour, then every 6 hours.
- **Battery from an INA219 / INA226:** an INA219 in the battery line gives voltage and signed current instead of the board's ADC; with `-D INA226_BATTERY=1` the same for an INA226. Charge counting: a real %, hours left, today's and yesterday's mAh.
- **Solar boards:** BQ25798 (xiao-solar-lite v0.5, xiao-solar) and CN3791 with two INA226 (xiao-solar-lite v0.6). `Xiao_nrf52_solar_repeater` is one firmware for every XIAO solar board, found at boot (`solar`, `charger` commands). Details: `docs/solar-boards.bg.md` (Bulgarian).
- **`battsave` power steps:** below 3.6 V adverts go half as often, below 3.45 V no LoRa OTA and no mailbox; the "off" step is not on by default (`battsave 3600 3450 3300`).
- **`selftest`:** one line for the app's Health page and for people. **Telemetry history:** 24 h every 10 minutes and a week every hour (not on STM32).
- **Packet filter and block list** (off by default): `filter ...` in the CLI, see `docs/packet_filter.md` (not in STM32 builds).
- **Fixes to upstream** in shared code: a crash on short frames from the app, malformed radio packets, atomic saves on nRF52 (contacts, channels, prefs), a race in the nRF52 BLE RX queue, and more.

> Status: the mailbox, LoRa OTA and the bootloader pass their unit and host tests and build in CI, but have not been tested on hardware yet; neither have battsave, selftest, telemetry history and the app's Health pages. The solar boards are marked "not hardware-tested yet".

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
