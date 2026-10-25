> **This is a fork of [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore).**
> It tracks upstream and adds a Faketec v5 companion build with its own encoder-driven UI (read and write messages without a phone), a mailbox and LoRa OTA updates for repeaters (with an Android app and an nRF52 bootloader that survives power cuts), light builds of any board, plus robustness fixes to the shared MeshCore code.
> Everything below "About MeshCore" is the upstream README. For general MeshCore support, issues and contributions, please use the upstream repository.

## 🍴 What this fork adds

- [Faketec v5 companion with encoder UI](#faketec-v5-companion-with-encoder-ui): hardware, wiring, controls, writing messages, settings, build flags
- [Repeater mailbox](#repeater-mailbox-store--forward-for-direct-messages): keeps direct messages for owners who are away
- [LoRa OTA for repeaters](#lora-ota-for-repeaters-nrf52-esp32): delta updates over the air from an Android app
- [nRF52 bootloader for LoRa OTA](#nrf52-bootloader-for-lora-ota): staged install that survives a power cut, rollback on boards with QSPI flash
- [Fixes to upstream MeshCore](#fixes-to-upstream-meshcore): crash and hang fixes that apply to every board
- [New shared components](#new-shared-components): reusable drivers and hooks added under `src/` and `examples/companion_radio/`
- [Builds and CI](#builds-and-ci)

> Status: the Faketec v5 firmware compiles and boots with BLE. The UI has been checked in the screen simulator (`tools/ui-sim`); the latest changes (network tools, channels from the device, learned words, hardware check, buttons instead of the encoder, repeat) have not been tested on hardware yet. The repeater mailbox, LoRa OTA and the bootloader pass their unit and host tests and build in CI, but have not been tested on hardware yet either.

### Faketec v5 companion with encoder UI

A companion radio build for a Faketec v5 (rev A) board without the on-board BMS/MOSFET, with a local screen and jog-dial controls so the node can be used without a phone. User manual in Bulgarian: [docs/faketec-companion.bg.md](./docs/faketec-companion.bg.md).

| Part | Used here |
|---|---|
| MCU | Nice!Nano v2 / nRF52840 Pro Micro clone |
| Radio | Heltec HT-RA62 (SX1262) |
| Display | 128x64 SSD1306 I2C OLED (e.g. WINSTAR WEA012864) |
| Encoder | Quadrature rotary encoder with push switch (e.g. ZIPPY AN-P1S-2SP-Z), or three buttons instead (see below) |
| Buttons | User button (Back) and reset; use 2-pin switches, a 4-pin switch shorts on the SW1/SW2 footprint |
| Haptics | 3 V coin vibration motor on the board's T2 MOSFET footprint (optional) |
| Power | 1S LiPo straight from the controller's charger, battery read through a divider |

**Wiring**

| Function | Pin |
|---|---|
| Encoder A / B | P1.01 / P1.02 (common to GND) |
| Encoder push | P1.07 |
| Buttons instead of the encoder | Up P1.01, Down P1.02, Enter P1.07 (each to GND) |
| User button (Back) | P1.00 |
| OLED SDA / SCL | P1.04 / P0.11 (board I2C) |
| Vibration motor | T2 gate = P0.08, motor between B+ and drain pad J7, flyback diode across the motor |

**Controls** (Sony Ericsson CMD-Z7 style jog dial, UI in [`examples/companion_radio/ui-encoder`](./examples/companion_radio/ui-encoder))

| Input | Action |
|---|---|
| Turn | Move in lists, scroll a conversation; on standby, next status card (summary, recently heard, radio, battery, GPS) |
| Press | Open / confirm; in a conversation, reply to the selected message |
| Hold press | Quick menu (advert nearby, advert to the whole mesh, transmit on/off, Bluetooth, GPS, power off) or the actions of the current item |
| Back (BTN) | Back one level; on standby, screen off |
| Hold Back | Standby from anywhere; on standby, lock the keys (hold Back again to unlock); while the boot splash shows, CLI rescue |
| Hold Back + press 2.5 s | Power off ("Power off" shows, a double buzz confirms; does nothing while the keys are locked). To switch on, hold Back + press together ~1.5 s until it buzzes; a shorter press goes back to sleep. With USB power it starts at once. In quiet mode neither buzzes |

**Messages**

- **Messages** lists conversations (contacts and channels), newest first, with the unread count or the age of the last message. A conversation opens as a chat: every message wrapped over as many rows as it needs, my messages with their delivery icon, the age of the selected one in the title. Press replies, hold opens Reply / Resend / Delete.
- **Channels** opens a channel's conversation (or the reply picker when it has none yet). Hold on a channel: write, mute (no buzz or screen wake, except for mentions; stored in `/ui_mute`) or delete. The last row, **+ Add**, joins a `#hashtag` channel typed on the keyboard (key = first 16 bytes of `sha256("#name")`, the same as the app).
- **Message details:** hold on a message > Details shows the time, the route (direct / flood / hops), SNR and RSSI of a received message, or the attempts and state of a sent one.
- **Reply** first asks **Quick reply** or **Write**. A quick reply can be sent as is or finished on the keyboard. Replies go to where the message came from (contact or channel).
- **Sending** waits 3 s with a progress bar; Back cancels it. Direct messages are tracked: a clock until the ACK, then a check mark. Without an ACK the message is sent again automatically with the same timestamp (the recipient shows it once), the last attempt as a flood; after 3 attempts it is marked not delivered with a long vibration, and **Resend** starts a new round. Channel messages have no ACK and show as sent.
- **Mentions:** a channel message with `@name` or `@[name]` for this node vibrates and wakes the screen like a direct message.
- **Recipient list:** channels (`#`) first, then chat and room contacts, sorted by name or by last heard. Sorted by name, a row of first letters sits on top: turn left from the first entry, press, turn to a letter, press to jump to the first name with it.
- **Keyboard:** BG, EN and 123 layouts with shift (automatic capital at the start and after `. ! ?`), space, language and send keys. Turn to pick a key, press to type, Back deletes, hold opens Send / Clear / Discard. An unsent message is kept for its recipient while you write elsewhere.
- **Next word:** after a space, the word you most often sent after the previous one is suggested (pairs learned from sent messages, up to 192, ~3.5 KB in `/ui_pairs`; punctuation breaks the chain; Settings > Messages > Learned words > Clear also clears them).
- **Word prediction:** the most frequent dictionary word for what you are typing is shown after the cursor and is selected right after each letter: **press** accepts it and adds a space, turning goes back to the keys. The dictionary (6000 Bulgarian + 4000 English words, ~71 KB flash) is chosen by the script you type in; words you typed recently are suggested first. It is generated by [`ui-encoder/tools/gen_dict.py`](./examples/companion_radio/ui-encoder/tools/gen_dict.py) from [FrequencyWords](https://github.com/hermitdave/FrequencyWords) (CC-BY-SA-4.0). Learned words: words you send that the dictionary lacks (names, places, slang; 3+ letters) are learned, up to 128, stored in `/ui_words`, and suggested before dictionary words.
- **Nothing is lost before the app syncs:** every message for the phone app is written to flash as it arrives (`-D OFFLINE_QUEUE_PERSIST`, an append-only log) and leaves it only when the app has fetched it, so it survives a reset, a hang or a flat battery (up to 256 messages; when full, the oldest channel messages go first). The device's own history (last 24) is not cleared by a sync; it is written within 10 minutes of a change, at power off and before an update. Messages sent from the device do not appear in the phone app's history.

- **Quiet hours:** Settings > Messages > Quiet hours (off, 22–07, 23–07, 00–07, 22–06, 23–06): in those hours channel messages neither wake the screen nor vibrate; direct messages and mentions still do. Only with a valid clock (from the app or an advert); without one it is off.
- **"I'm back":** after a power-on or a wake from sleep/hibernate, and when a repeater is heard again after 10+ minutes of silence, the companion sends one zero-hop advert by itself (at most once in 10 minutes, only while transmit is on), so a repeater's mailbox hands over at once (`-D BACK_ADVERT`). When the mailbox delivers, the screen shows "Mail: N messages".
- **Battery:** the battery card shows the voltage and the time left (e.g. `3.8V ~18h`), from how fast the percentage fell over the last hours; the next card is a 24-hour graph of the battery with its low–high %. The history (every 10 minutes for 24 h, every hour for a week, `-D TELEMETRY_HISTORY`) is kept in `/tele_hist` and the app reads it (BLE command 121).

**Network** (Menu > Network)

- **Nearby:** a zero-hop discover request; the repeaters that answer in 10 s, strongest first. A repeater shows how well it hears us (↑) and we hear it (↓), and starts a trace or range test to it. Hold searches again.
- **Trace:** from a contact (hold > Trace) or a nearby repeater: a TRACE packet there and back through the repeaters of its path, with the SNR at each hop and the round-trip time.
- **Airtime:** transmit time in the last hour against the EU 868 MHz 10% duty cycle, total TX time, packets sent / received / errors, noise floor, last SNR and RSSI.
- **Range test:** a trace to the chosen repeater or contact every 15 / 30 / 60 / 120 s, a buzz for each answer (a different one for a miss), the last results with SNR there and back. Keeps running with the screen off until stopped (an icon in the standby status row shows it); stops if transmit is switched off.

**Settings** (Menu > Settings, saved in prefs)

| Group | Settings |
|---|---|
| Display | menu font and text font size (large / medium / small / tiny), screen timeout (15 s – 5 min), brightness, page dots |
| Hardware | encoder direction, encoder steps per detent (2 / 4; hidden with buttons), vibration, keyboard jump to the likely next letter, key lock (by hand / automatic when the screen goes off), input test, vibration test, screen test, readings (battery raw/ADC, USB, reset reason, noise floor, uptime) |
| Messages | alerts (without phone / always / never), wake the screen for (direct and mentions / all / never), quiet hours, confirm deletes, contacts sort, 10 quick replies (stored in `/ui_quick`), learned words (count, clear) |
| Radio | transmit on/off, TX power, antenna question (after a new battery / every boot / never), preset (EU/UK Narrow, EU/UK Deprecated; anything else shows as custom), repeater |
| System | node name (an advert with the new name is offered; the Bluetooth name changes after a restart), language BG / EN, time zone, EU summer time, battery calibration, about, CLI rescue, firmware update over USB (UF2 drive) or BLE (DFU) |

**Buttons instead of the encoder:** Up on P1.01, Down on P1.02 and Enter on P1.07, each to GND. The firmware is the same: after a new battery, the first turn or press at the antenna question shows which is fitted, and that is remembered across restarts and power off. A held button repeats. A wrong guess fixes itself: two full encoder clicks in one direction within 1 s switch to the encoder, two presses of one button within 2 s switch to buttons.

**Repeater (emergency only):** Settings > Radio > Repeater (or the phone app) makes the companion repeat other nodes' packets. It loads the mesh and the battery, so it asks "Emergency only! Turn on?" first, and a tower icon shows in the status row while it is on. It is allowed on 433.000, 869.495, 869.618 (EU/UK Narrow) and 918.000 MHz (`-D ALLOWED_REPEAT_FREQ_RANGE` in the Faketec ini); while it is on, a preset on another frequency is refused.

**Battery calibration:** Settings > System > Battery shows the measured voltage; set it to what a multimeter shows at the battery and the divider factor is saved in prefs. The shown value is averaged, and the node powers off only after three low readings in a row, so a transmit pulse does not switch it off.

**Watchdog:** the nRF52 hardware watchdog (30 s, `-D WATCHDOG_SECONDS`) restarts a node whose main loop hangs, e.g. on a stuck I2C bus, so the low-battery cutoff keeps working. The bootloader feeds it, so DFU and UF2 updates are not affected.

**Build flags** (in [`variants/faketec_v5/platformio.ini`](./variants/faketec_v5/platformio.ini))

- `-D DISPLAY_CYRILLIC`: Cyrillic 5x8 font for the SSD1306.
- `-D PIN_VIBRATION`, `-D VIBRATION_PWM=190`: haptic patterns (key click, confirm, channel / direct message, power off); PWM keeps the motor near 3.1 V on a full LiPo. Remove to build without a motor.
- `-D AUTO_SHUTDOWN_MILLIVOLTS=3300`: there is no BMS, so the node hibernates below this voltage. Calibrate in Settings > System > Battery (or set `-D ADC_MULTIPLIER` for your divider).
- `-D ENCODER_STEPS_PER_DETENT=2`, `-D ENCODER_REVERSE=true`: build defaults for the encoder; both can also be changed in Settings.
- `-D DISABLE_RTC_AUTODISCOVERY`, `-D DISABLE_ENV_SENSORS`: skip the RTC probes and the sensor scan when none are fitted (a faster boot; a bad I2C bus no longer hangs it, see below).
- `-D ALLOWED_REPEAT_FREQ_RANGE=...`: the frequencies where the repeater setting may be turned on (upstream allows 433.000, 869.495 and 918.000 MHz; this build adds 869.618).

Build with PlatformIO, e.g. `pio run -e Faketec_v5_companion_radio_ble` (or `_usb`), then flash the UF2 by double-tapping reset and copying it to the bootloader drive.

### Repeater mailbox (store & forward for direct messages)

Every repeater built from this fork (`-D NO_MAILBOX` leaves it out) can keep direct messages for up to 4 owner companions while they are away, and hand them over when the owner is heard again with no hop between. Nothing is flooded: messages are kept from traffic the repeater hears anyway, and handed over as zero-hop packets that nobody repeats.

- **What is kept:** only `TXT_MSG` packets addressed to an owner (by the first public key byte, the only one in the packet), still encrypted to the owner; the repeater cannot read them. Channel messages, adverts and messages sent with no hop (the owner is next to the sender, or another mailbox is handing over) are not kept. Off until `mailbox learn` or `mailbox on`. Up to 64 messages (8 on STM32) in flash (`/mailbox`), so they survive a restart; older than `ttl` hours are dropped (after a restart without a clock, the age counts from the restart); when full, the oldest goes.
- **Only while away:** an owner heard with no hop (its advert, or a flood packet it just sent) in the last `away` minutes is present, and nothing is kept for it. A flood message the owner answered with a PATH reply in the next 2 minutes is dropped as delivered. A sender's automatic retries (same sender, same length, within 30 s) are kept once.
- **Hand-over:** on any zero-hop packet from the owner (an advert, a message, a request, a PATH; the Faketec sends an advert by itself when it comes back), the kept messages go out one every 2.5 s as zero-hop direct packets. A packet other than an advert carries only the first key byte, which other nodes can share, so then one message goes at a time and the next only after the owner's ACK was heard (at most one such try a minute). The owner ACKs them as usual; a sender that is still on the mesh gets the late ACK, and the Faketec UI turns its "not delivered" into delivered.
- **Shown once:** the companion firmware drops a direct message it already showed (same sender and timestamp, last 32), so a kept copy or a retry is not shown twice; the sender still gets its ACK.
- **Count:** the number of kept messages is in `selftest` (`mail=N`) and on the app's Health page.
- **CLI** (serial or remote admin): `mailbox` (state), `mailbox on|off`, `mailbox learn` (the next companion advert with no hop in 2 minutes becomes an owner), `mailbox add <64-hex key>`, `mailbox del <key prefix>`, `mailbox list`, `mailbox ttl <hours>` (default 72), `mailbox away <minutes>` (default 10), `mailbox clear`.

### LoRa OTA for repeaters (nRF52, ESP32)

- **How:** the Android app in [`android/`](./android) makes a delta from the firmware the repeater runs to the new one and sends it through your companion, only with no hop, on a side channel (default 869.470 MHz / 125 kHz / SF7) so the main channel keeps working.
- **Off by default:** run `set ota on` in the repeater's console first (`set ota off` turns it off again, `ota status` shows it).
- **Cut short:** the repeater goes back to its normal channel and keeps its progress for 30 minutes; the same update in the app continues where it stopped.
- **Time on air:** the app keeps to 10 % of each hour; a bigger delta waits for the next part of the hour on the normal channel.
- **nRF52 storage:** the new image goes to the free internal flash after the running one, then is copied over it just before the restart. Repeaters with a QSPI flash (Xiao nRF52840, Ikoka Stick/Nano/Handheld, T-Echo, ThinkNode M1/M6, Wio Tracker L1 and L1 1W, SenseCAP Solar; `-D QSPI_STORAGE`) keep it there instead, together with the mailbox (a ring of 24 slots) and a copy of the running image for rollback (832 KB each, more than the whole app area). Boards where the old and the new image do not fit together (e.g. RAK4631, about 460 KB) update **in place**: the repeater keeps the delta and the end of the image, and makes the start again in RAM just before the copy. The app shows a big warning for it.
- **Our bootloader (nRF52):** with [our bootloader](#nrf52-bootloader-for-lora-ota) the final copy survives a power cut, and boards with QSPI flash get rollback.
- **Power cut on nRF52 without it:** during the final copy (about 20 s) a power cut leaves the repeater in the bootloader with no firmware; it has to be updated on site (USB or BLE DFU). The stock bootloader then at least checks the image's CRC at every start. ESP32 has two app slots and is not at risk.
- **Low battery:** the final copy waits while the repeater runs on a battery below 3.7 V (`-D OTA_MIN_BATT_MV`); the update is paused and the app offers to continue.
- **Watchdog on nRF52 repeaters:** 60 s (`-D REPEATER_WATCHDOG_SECS`, 0 = off). It cannot be stopped: after loading firmware that does not feed it, press RST once.
- **New firmware must prove its radio:** a new image (ESP32, or nRF52 with rollback) is confirmed by an admin login, or once it has sent and received a packet within 10 minutes (it sends an advert at 5 minutes if it has not sent anything). With nothing heard by 60 minutes it is still confirmed when its noise floor is sane (-140..-60 dBm, a quiet area), otherwise it rolls back. `ota rollback` does it by hand.
- **Where the firmware comes from:** the app offers three sources for both an update over LoRa and a flash over Bluetooth / Wi-Fi: **Releases** (ours with `ota-manifest.json`, and for a flash also stock MeshCore from meshcore-dev), **New build** (runs [Build Firmware for Boards](#builds-and-ci) on GitHub with the board picked from the device and the workflow's own boxes, then downloads the run's artifact and checks it against its `.ota.json`) and **File**. A build needs a fine-grained GitHub token for this repository with *Actions: Read and write* and *Contents: Read*; it is kept on the phone only, encrypted with an Android Keystore key. The app follows the run in the background and notifies when it is done. For a group: Actions > Build LoRa OTA Android App > Run workflow with *group APK* builds a second APK with the token from the secret `LORAOTA_GITHUB_TOKEN` (XOR-ed, not readable as text, but anyone who takes the APK apart can get it), only as an artifact kept 3 days, never in a release.
- **Flash from a file:** `.zip` (DFU package), `.bin` (Wi-Fi, ESP32) or `.uf2`; a `.uf2` is turned into a DFU `.zip` in the app (application only, any SoftDevice), so the same file as for the USB drive works over Bluetooth. A bootloader or SoftDevice `.uf2` is refused.
- **Bluetooth flash log:** the flash screen shows the DFU library's log while it runs and keeps it after an error, with a Copy button. A repeater is flashed without a bond and the app looks for its bootloader by scanning (the Adafruit bootloader advertises on the address + 1 after a jump from the app).
- **Battery and current:** after login the app reads the repeater's telemetry; with an INA219 it shows the battery voltage, the current and whether it is charging or discharging.
- **Backup:** before every flash and LoRa update the app saves the device's settings (companion: settings, channels, contacts; repeater: its CLI settings and the admin password kept in the app), and can restore them by difference or export them to a file (encrypted with a password; without one the secrets stay out).
- **Health:** a page for the repeater (and the companion): the repeater's `selftest` (radio, I2C, QSPI, battery with time left and today's mAh in/out, mailbox, OTA, power step, uptime), kept on the phone at every check; warnings for a dead radio, a sensor turned off (and newly off since the last check), a stuck bus, a low-battery step, unconfirmed firmware, and a battery that falls at least twice as fast as its weekly average. Graphs for 24 h and a week of battery %, voltage, current, temperature, humidity, pressure, noise, packets and airtime (repeater request `0x41`, companion BLE command 121).
- **Several repeaters at once:** "Update several repeaters" picks a release and the repeaters with a kept password; it logs in, backs up and updates them one after another (no-hop routes first), keeps to the shared airtime limit, saves the queue after each one so it goes on after an interruption, and ends with a report (updated / already up to date / skipped and why / failed). A repeater that needs an in-place copy is skipped (do it by hand).
- **APK signing:** CI signs with the key from the secrets `LORAOTA_KEYSTORE_BASE64`, `LORAOTA_KEYSTORE_PASSWORD`, `LORAOTA_KEY_ALIAS`, `LORAOTA_KEY_PASSWORD` (a debug key when they are not set).

### Repeater battery from an INA219

With an INA219 in the battery line (the battery on Vin+, the load and the charger on Vin-), the repeater takes the battery from it instead of the board's ADC: bus voltage plus the drop over the shunt. It is used in the status, telemetry channel 1, the `power` command and the battery check before a LoRa OTA install. The INA219 channel also carries the current with its sign as an analog input (Cayenne LPP current has no sign): negative while charging, positive while discharging. Without an INA219, or after it was turned off for I2C errors (see `i2c` below), the board's own reading is used.

### Repeaters that look after themselves

- **Charge counting (INA219):** with the current sensor the repeater counts the charge in and out: a real %, hours left at the 6-hour average, and today's / yesterday's mAh in and out. The capacity is learnt from a full charge (4.15 V with only a trickle going in) down to empty (3.3 V), or set with `battery capacity <mAh>`. In telemetry, `selftest` and `battery`.
- **Power steps:** below 3.6 V adverts go half as often and the mailbox stops writing to flash; below 3.45 V no LoRa OTA and no mailbox; below 3.3 V the radio is off and the node only checks the battery (ESP32 deep sleep 30 min; nRF52 every 10 min) and restarts when it is charged again (+150 mV) or on external power. 3 readings in a row decide a step down, 100 mV above the limit a step up. `powersave` shows and sets it.
- **Sensors come back:** a sensor turned off after I2C errors is tried again after 10 minutes, 1 hour, then every 6 hours; `i2c` shows how often it dropped out.
- **Self test:** `selftest` gives one line for the app's Health page and for people (see [docs](./docs/cli_commands.md#self-test)).
- **Telemetry history:** every 10 minutes for 24 h and every hour for a week (battery, current, temperature, humidity, pressure, noise, packets, airtime), kept in `/tele_hist` (saved every hour), read by the app with request `0x41` (not on STM32).

### nRF52 bootloader for LoRa OTA

[`bootloader/`](./bootloader) is the [OTAFIX bootloader](https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX) (pinned commit) with one addition, a **staged install**. Details, layout and build: [bootloader/README.md](./bootloader/README.md).

- **What it changes:** the repeater puts the whole new image where the copy does not touch it (the QSPI flash, or the internal flash after the running image), writes a small record in the bootloader settings page and restarts. The bootloader checks the image (CRC-32), copies it, checks the result and marks the record. The source is never written, so a power cut during the copy only makes it start again.
- **Rollback (boards with QSPI flash):** the running image is also kept in the QSPI flash and the new one runs on trial. It is confirmed by an admin login, an INFO from the LoRa OTA app or 10 minutes of uptime; after 3 restarts without that (a crash, or a hang caught by the watchdog) the bootloader copies the old image back. `ota rollback` on the repeater does it by hand.
- **Old bootloader:** firmware from this fork still updates the old way (RAM copy), and now also stores the image's CRC16, which the stock bootloader checks at every start.

| What a repeater gets | Boards |
|---|---|
| staged install + rollback (`-D QSPI_STORAGE` in the repeater env) | Xiao nRF52840, Ikoka Stick / Nano / Handheld (Xiao bootloader), LilyGo T-Echo, ThinkNode M1, ThinkNode M6, Wio Tracker L1 and L1 1W, SenseCAP Solar P1 |
| staged install, normal build (image up to 396 KB) | ProMicro / Faketec, T1000-E, ThinkNode M3, Heltec T114 without display, RAK WisMesh Tag, Minewsemi ME25LS01 |
| staged install only with a light build (`build-boards.yml`, about 310-336 KB) | RAK4631, RAK3401, Heltec T114 with display, Heltec T096, Heltec T1 |

Bigger images still update over LoRa, the old way. T114 and T1000-E have QSPI flash, but its pins are not known yet; RAK boards have at most 1 MB of QSPI flash, too small for the layout.

**Install** (once per node): get `update-<board>_bootloader-…-mcota1_nosd.uf2` from a `bootloader-v*` release or the Build Bootloader workflow's artifacts, double-tap RST and copy it to the drive. `INFO_UF2.TXT` on the drive names your board; on a Xiao pick the Sense file only if it says `SeeedXiaoSense`. The firmware finds the new bootloader by itself; nothing else needs changing.

### Fixes to upstream MeshCore

These are in shared code and apply to every board.

- **Crash on short frames from the app:** `CMD_SEND_CHANNEL_TXT_MSG` shorter than its 7-byte header made the text length negative and `sendGroupMessage()` copied ~4 GB. All command handlers that read fixed-size fields now check the frame length first, instead of reading stale bytes from the previous frame; `REBOOT` / `FACTORY_RESET` could previously match text left over from an earlier frame. `SEND_RAW_DATA` path length and `SET_DEFAULT_FLOOD_SCOPE` name scan are bounded.
- **Malformed radio packets:** a `PAYLOAD_TYPE_PATH` whose path runs past the data no longer underflows the extra length and overruns `out_frame`; `TRACE` packets need tag, auth and flags before the hop offset is computed; contact responses are clamped to the frame size.
- **Invalid path lengths:** an invalid `out_path_len` from the app or from `/contacts3` is treated as unknown (flood) instead of producing malformed direct packets.
- **nRF52 BLE RX race:** the RX queue was shifted in `loop()` while the Bluefruit callback appended to it without locking, so frames could be lost or duplicated. It is now a single-producer / single-consumer ring; connect / disconnect request a flush that `loop()` performs.
- **Atomic saves on nRF52:** contacts, channels and prefs are written to a temp file and renamed, so a reset or brown-out during a save no longer corrupts them (falls back to an in-place write when the filesystem has no room for a second copy).
- **Hang on the I2C bus (nRF52):** the Adafruit Wire driver waits on the TWIM with no timeout and reads an error left by an earlier transfer as its own, so a loose cable or a module holding a line could stop a node at boot or in a sensor read. [`arch/nrf52/libs/nrf52_wire`](./arch/nrf52/libs/nrf52_wire) is that driver with a 50 ms timeout on every wait (`-D WIRE_TIMEOUT_MS`), a reset of the TWIM, bus recovery (9 SCL clocks and a STOP), stale events cleared before each transfer and error counters; it replaces the framework's Wire on every nRF52 build, so sensor libraries and displays get the timeouts too. A device that times out at boot, or whose reads fail 3 times in a row, is turned off until a reboot and the others go on (it keeps its telemetry channel). The CLI command `i2c` lists what was found and what was turned off and why ([docs](./docs/cli_commands.md#show-the-i2c-devices-and-the-ones-turned-off)). The flags `-D DISABLE_RTC_AUTODISCOVERY` and `-D DISABLE_ENV_SENSORS` still skip the RTC probes and the sensor scan.
- **Display left on after power-off:** `ui-new` and `ui-tiny` put a non-e-ink display to sleep before SYSTEMOFF, so the OLED no longer drains the battery after hibernate or low-battery shutdown.
- **STM32 builds:** `[stm32_base]` pins the STM32duino framework, because the new 3.0 release breaks RadioLib's Arduino HAL and drops `ltoa()`.

### New shared components

- `src/helpers/ui/QuadratureRotaryInput`: interrupt-driven quadrature decoder with 2 or 4 transitions per detent, resync at the rest position, runtime direction and steps; ISR-safe on ESP32.
- `src/helpers/ui/SSD1306Display` + `CyrillicFont5x8.h`: UTF-8 Cyrillic rendering (`-D DISPLAY_CYRILLIC`); characters the font cannot draw show as a block.
- `src/helpers/ui/GenericVibration`: PWM drive and multi-pulse patterns.
- `src/helpers/ui/MomentaryButton::setDebounce()`: debounce without the multi-click delay, so presses register at once.
- `AbstractUITask::msgSource()`: optional hook telling a UI whether the next message came from a contact or a channel (no-op for the other UIs).
- `DataStore::loadSmallFile()` / `saveSmallFile()`: small UI-owned files with the same atomic save.
- `src/helpers/I2CHealth`: I2C probe with a timeout result, the error counters and the list `i2c` shows.
- `SensorManager::getBatteryMilliVolts()`: a battery from a sensor (the INA219), 0 when there is none.

### Builds and CI

- [`.github/workflows/build-boards.yml`](./.github/workflows/build-boards.yml) builds chosen boards by hand (Actions > Build Firmware for Boards > Run workflow): pick a repeater / companion / room server from the drop-downs, type more env names, or add a whole group (a filter such as `Xiao` narrows only the group; picked boards are always built). Light by default: RTC, sensors, GPS, screen driver and debug logs stay out unless ticked (without the display box the screen is a no-op but buttons, LED and buzzer keep working; boards that start their GPS in their own code keep it) (mailbox and LoRa OTA stay in unless unticked), and such files get `-lite` after the board name; [`tools/lite-flags.sh`](./tools/lite-flags.sh) gives the same flags for a local build. After new boards arrive, `bash tools/gen-board-choices.sh` refreshes the drop-downs. Files go to the run's artifacts, and optionally to a GitHub pre-release. The LoRa OTA app can start it too (input `request_id`, which names the run so the app finds it).
- [`.github/workflows/build-xiao-repeaters.yml`](./.github/workflows/build-xiao-repeaters.yml) builds the Xiao repeaters with the `.bin` files and `ota-manifest.json` the LoRa OTA app uses; a `xiao-repeater-v*` tag publishes a release.
- [`.github/workflows/build-bootloader.yml`](./.github/workflows/build-bootloader.yml) tests and builds the nRF52 bootloader for every OTAFIX board (on changes to `bootloader/` and by hand); a `bootloader-v*` tag publishes the `update-*.uf2` files.
- [`.github/workflows/build-android-app.yml`](./.github/workflows/build-android-app.yml) tests and builds the LoRa OTA app; a `lora-ota-app-v*` tag publishes the APK.
- **Size check:** [`tools/check-size.sh`](./tools/check-size.sh) writes each build's image, flash and RAM to the run summary. It fails an nRF52 repeater with LoRa OTA and no QSPI flash whose image is over 396 KB (no room for the staged copy; in Build Firmware for Boards the files are still uploaded) and a Faketec build over 95 % of its flash; the PR check only warns.
- [`.github/workflows/build-faketec-v5.yml`](./.github/workflows/build-faketec-v5.yml) builds the BLE and USB firmware on pushes to `main` and on manual runs, and uploads the `.uf2` and OTA `.zip` as run artifacts. A `faketec-v*` tag also publishes a GitHub release with them.

## About MeshCore

MeshCore is a lightweight, portable C++ library that enables multi-hop packet routing for embedded projects using LoRa and other packet radios. It is designed for developers who want to create resilient, decentralized communication networks that work without the internet.

## 🔍 What is MeshCore?

MeshCore now supports a range of LoRa devices, allowing for easy flashing without the need to compile firmware manually. Users can flash a pre-built binary using tools like Adafruit ESPTool and interact with the network through a serial console.
MeshCore provides the ability to create wireless mesh networks, similar to Meshtastic and Reticulum but with a focus on lightweight multi-hop packet routing for embedded projects. Unlike Meshtastic, which is tailored for casual LoRa communication, or Reticulum, which offers advanced networking, MeshCore balances simplicity with scalability, making it ideal for custom embedded solutions, where devices (nodes) can communicate over long distances by relaying messages through intermediate nodes. This is especially useful in off-grid, emergency, or tactical situations where traditional communication infrastructure is unavailable.

## ⚡ Key Features

* Multi-Hop Packet Routing
  * Devices can forward messages across multiple nodes, extending range beyond a single radio's reach.
  * Supports up to a configurable number of hops to balance network efficiency and prevent excessive traffic.
  * Nodes use fixed roles where "Companion" nodes are not repeating messages at all to prevent adverse routing paths from being used.
* Supports LoRa Radios – Works with Heltec, RAK Wireless, and other LoRa-based hardware.
* Decentralized & Resilient – No central server or internet required; the network is self-healing.
* Low Power Consumption – Ideal for battery-powered or solar-powered devices.
* Simple to Deploy – Pre-built example applications make it easy to get started.

## 🎯 What Can You Use MeshCore For?

* Off-Grid Communication: Stay connected even in remote areas.
* Emergency Response & Disaster Recovery: Set up instant networks where infrastructure is down.
* Outdoor Activities: Hiking, camping, and adventure racing communication.
* Tactical & Security Applications: Military, law enforcement, and private security use cases.
* IoT & Sensor Networks: Collect data from remote sensors and relay it back to a central location.

## 🚀 How to Get Started

- Watch the [MeshCore QuickStart Playlist](https://www.youtube.com/watch?v=iaFltojJrAc&list=PLshzThxhw4O4WU_iZo3NmNZOv6KMrUuF9) by The Comms Channel
- Watch the [MeshCore Technical Presentation](https://www.youtube.com/watch?v=OwmkVkZQTf4) by Liam Cottle.
- Read through our [Frequently Asked Questions](./docs/faq.md) and [Documentation](https://docs.meshcore.io).
- Flash the MeshCore firmware on a supported device.
- Connect with a supported client.

For developers:

- Install [PlatformIO](https://docs.platformio.org) in [Visual Studio Code](https://code.visualstudio.com).
- Clone and open the MeshCore repository in Visual Studio Code.
- See the example applications you can modify and run:
  - [Companion Radio](./examples/companion_radio) - For use with an external chat app, over BLE, USB or Wi-Fi.
  - [KISS Modem](./examples/kiss_modem) - Serial KISS protocol bridge for host applications. ([protocol docs](./docs/kiss_modem_protocol.md))
  - [Simple Repeater](./examples/simple_repeater) - Extends network coverage by relaying messages.
  - [Simple Room Server](./examples/simple_room_server) - A simple BBS server for shared Posts.
  - [Simple Secure Chat](./examples/simple_secure_chat) - Secure terminal based text communication between devices.
  - [Simple Sensor](./examples/simple_sensor) - Remote sensor node with telemetry and alerting.

The Simple Secure Chat example can be interacted with through the Serial Monitor in Visual Studio Code, or with a Serial USB Terminal on Android.

## ⚡️ MeshCore Flasher

We have prebuilt firmware ready to flash on supported devices.

- Launch https://meshcore.io/flasher
- Select a supported device
- Flash one of the firmware types:
  - Companion, Repeater or Room Server
- Once flashing is complete, you can connect with one of the MeshCore clients below.

## 📱 MeshCore Clients

**Companion Firmware**

The companion firmware can be connected to via BLE, USB or Wi-Fi depending on the firmware type you flashed.

- Web: https://app.meshcore.nz
- Android: https://play.google.com/store/apps/details?id=com.liamcottle.meshcore.android
- iOS: https://apps.apple.com/us/app/meshcore/id6742354151?platform=iphone
- NodeJS: https://github.com/liamcottle/meshcore.js
- Python: https://github.com/fdlamotte/meshcore-cli

**Repeater and Room Server Firmware**

The repeater and room server firmware can be set up via USB in the web config tool.

- https://config.meshcore.io

They can also be managed via LoRa in the mobile app by using the Remote Management feature.

## 🛠 Hardware Compatibility

MeshCore is designed for devices listed in the [MeshCore Flasher](https://meshcore.io/flasher)

## 📜 License

MeshCore is open-source software released under the MIT License. You are free to use, modify, and distribute it for personal and commercial projects.

## Contributing

Please submit PR's using 'dev' as the base branch!
For minor changes just submit your PR and we'll try to review it, but for anything more 'impactful' please open an Issue first and start a discussion. It is better to sound out what it is you want to achieve first, and try to come to a consensus on what the best approach is, especially when it impacts the structure or architecture of this codebase.

Here are some general principles you should try to adhere to:
* Keep it simple. Please, don't think like a high-level lang programmer. Think embedded, and keep code concise, without any unnecessary layers.
* No dynamic memory allocation, except during setup/begin functions.
* Use the same brace and indenting style that's in the core source modules. (A .clang-format is probably going to be added soon, but please do NOT retroactively re-format existing code. This just creates unnecessary diffs that make finding problems harder)

Help us prioritize! Please react with thumbs-up to issues/PRs you care about most. We look at reaction counts when planning work.

### Running unit tests

To run unit tests, run the following command:

```bash
pio test --environment native --verbose
```

This fork also has a host test of the bootloader's staged install, with simulated power cuts at every step (`sh bootloader/test/run.sh`), the UI screen tests (`sh tools/ui-sim/build.sh`), and the Android app's tests (`cd android && ./gradlew :delta:test :app:testDebugUnitTest`). CI runs the first three in `run-unit-tests.yml`, and the app's in `build-android-app.yml`.

## Road-Map / To-Do

There are a number of fairly major features in the pipeline, with no particular time-frames attached yet. In very rough chronological order:
- [X] Companion radio: UI redesign
- [X] Repeater + Room Server: add ACL's (like Sensor Node has)
- [X] Standardise Bridge mode for repeaters
- [ ] Repeater/Bridge: Standardise the Transport Codes for zoning/filtering
- [X] Core + Repeater: enhanced zero-hop neighbour discovery
- [ ] Core: round-trip manual path support
- [ ] Companion + Apps: support for multiple sub-meshes (and 'off-grid' client repeat mode)
- [ ] Core + Apps: support for LZW message compression
- [ ] Core: dynamic CR (Coding Rate) for weak vs strong hops
- [ ] Core: new framework for hosting multiple virtual nodes on one physical device
- [ ] V2 protocol spec: discussion and consensus around V2 packet protocol, including path hashes, new encryption specs, etc

## 📞 Get Support

- Report bugs and request features on the [GitHub Issues](https://github.com/meshcore-dev/meshcore/issues) page.
- Find additional guides and components on [my site](https://buymeacoffee.com/ripplebiz).
- Join [MeshCore Discord](https://meshcore.gg) to chat with the developers and get help from the community.
