> **This is a fork of [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore).**
> It tracks upstream and adds a Faketec v5 companion build with its own encoder-driven UI (read and write messages without a phone), plus robustness fixes to the shared MeshCore code.
> Everything below "About MeshCore" is the upstream README. For general MeshCore support, issues and contributions, please use the upstream repository.

## 🍴 What this fork adds

- [Faketec v5 companion with encoder UI](#faketec-v5-companion-with-encoder-ui): hardware, wiring, controls, writing messages, settings, build flags
- [Fixes to upstream MeshCore](#fixes-to-upstream-meshcore): crash and hang fixes that apply to every board
- [New shared components](#new-shared-components): reusable drivers and hooks added under `src/` and `examples/companion_radio/`
- [Builds and CI](#builds-and-ci)

> Status: the Faketec v5 firmware compiles and boots with BLE. The UI has been checked in the screen simulator (`tools/ui-sim`); the latest changes (network tools, channels from the device, learned words, hardware check) have not been tested on hardware yet.

### Faketec v5 companion with encoder UI

A companion radio build for a Faketec v5 (rev A) board without the on-board BMS/MOSFET, with a local screen and jog-dial controls so the node can be used without a phone. User manual in Bulgarian: [docs/faketec-companion.bg.md](./docs/faketec-companion.bg.md).

| Part | Used here |
|---|---|
| MCU | Nice!Nano v2 / nRF52840 Pro Micro clone |
| Radio | Heltec HT-RA62 (SX1262) |
| Display | 128x64 SSD1306 I2C OLED (e.g. WINSTAR WEA012864) |
| Encoder | Quadrature rotary encoder with push switch (e.g. ZIPPY AN-P1S-2SP-Z) |
| Buttons | User button (Back) and reset; use 2-pin switches, a 4-pin switch shorts on the SW1/SW2 footprint |
| Haptics | 3 V coin vibration motor on the board's T2 MOSFET footprint (optional) |
| Power | 1S LiPo straight from the controller's charger, battery read through a divider |

**Wiring**

| Function | Pin |
|---|---|
| Encoder A / B | P1.01 / P1.02 (common to GND) |
| Encoder push | P1.07 |
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
- **Word prediction:** the most frequent dictionary word for what you are typing is shown after the cursor and is selected right after each letter: **press** accepts it and adds a space, turning goes back to the keys. The dictionary (6000 Bulgarian + 4000 English words, ~71 KB flash) is chosen by the script you type in; words you typed recently are suggested first. It is generated by [`ui-encoder/tools/gen_dict.py`](./examples/companion_radio/ui-encoder/tools/gen_dict.py) from [FrequencyWords](https://github.com/hermitdave/FrequencyWords) (CC-BY-SA-4.0). Learned words: words you send that the dictionary lacks (names, places, slang; 3+ letters) are learned, up to 128, stored in `/ui_words`, and suggested before dictionary words.
- **Nothing is lost before the app syncs:** every message for the phone app is written to flash as it arrives (`-D OFFLINE_QUEUE_PERSIST`, an append-only log) and leaves it only when the app has fetched it, so it survives a reset, a hang or a flat battery (up to 256 messages; when full, the oldest channel messages go first). The device's own history (last 24) is not cleared by a sync; it is written within 10 minutes of a change, at power off and before an update. Messages sent from the device do not appear in the phone app's history.

**Network** (Menu > Network)

- **Nearby:** a zero-hop discover request; the repeaters that answer in 10 s, strongest first. A repeater shows how well it hears us (↑) and we hear it (↓), and starts a trace or range test to it. Hold searches again.
- **Trace:** from a contact (hold > Trace) or a nearby repeater: a TRACE packet there and back through the repeaters of its path, with the SNR at each hop and the round-trip time.
- **Airtime:** transmit time in the last hour against the EU 868 MHz 10% duty cycle, total TX time, packets sent / received / errors, noise floor, last SNR and RSSI.
- **Range test:** a trace to the chosen repeater or contact every 15 / 30 / 60 / 120 s, a buzz for each answer (a different one for a miss), the last results with SNR there and back. Keeps running with the screen off until stopped (an icon in the standby status row shows it); stops if transmit is switched off.

**Settings** (Menu > Settings, saved in prefs)

| Group | Settings |
|---|---|
| Display | screen timeout (15 s – 5 min), brightness, page dots, menu font and text font size (large / medium / small / tiny) |
| Controls | vibration, encoder direction, encoder steps per detent (2 / 4), keyboard jump to the likely next letter, key lock (by hand / automatic when the screen goes off) |
| Messages | alerts (without phone / always / never), wake the screen for (direct and mentions / all / never), confirm deletes, contacts sort, 10 quick replies (stored in `/ui_quick`), learned words (count, clear) |
| Radio | transmit on/off, TX power, antenna question (after a new battery / every boot / never), preset (EU/UK Narrow, EU/UK Deprecated; anything else shows as custom) |
| System | node name (an advert with the new name is offered; the Bluetooth name changes after a restart), language BG / EN, time zone, EU summer time, battery calibration, about, hardware check (encoder, vibration and display tests; battery raw/ADC, USB, reset reason, noise floor, uptime), CLI rescue, firmware update over USB (UF2 drive) or BLE (DFU) |

**Battery calibration:** Settings > System > Battery shows the measured voltage; set it to what a multimeter shows at the battery and the divider factor is saved in prefs. The shown value is averaged, and the node powers off only after three low readings in a row, so a transmit pulse does not switch it off.

**Watchdog:** the nRF52 hardware watchdog (30 s, `-D WATCHDOG_SECONDS`) restarts a node whose main loop hangs, e.g. on a stuck I2C bus, so the low-battery cutoff keeps working. The bootloader feeds it, so DFU and UF2 updates are not affected.

**Build flags** (in [`variants/faketec_v5/platformio.ini`](./variants/faketec_v5/platformio.ini))

- `-D DISPLAY_CYRILLIC`: Cyrillic 5x8 font for the SSD1306.
- `-D PIN_VIBRATION`, `-D VIBRATION_PWM=190`: haptic patterns (key click, confirm, channel / direct message, power off); PWM keeps the motor near 3.1 V on a full LiPo. Remove to build without a motor.
- `-D AUTO_SHUTDOWN_MILLIVOLTS=3300`: there is no BMS, so the node hibernates below this voltage. Calibrate in Settings > System > Battery (or set `-D ADC_MULTIPLIER` for your divider).
- `-D ENCODER_STEPS_PER_DETENT=2`, `-D ENCODER_REVERSE=true`: build defaults for the encoder; both can also be changed in Settings.
- `-D DISABLE_RTC_AUTODISCOVERY`, `-D DISABLE_ENV_SENSORS`: recommended when no RTC or sensors are fitted (see below).

Build with PlatformIO, e.g. `pio run -e Faketec_v5_companion_radio_ble` (or `_usb`), then flash the UF2 by double-tapping reset and copying it to the bootloader drive.

### Repeater mailbox (store & forward for direct messages)

Repeaters built with `-D WITH_MAILBOX` (`Xiao_C3_repeater`, `Xiao_nrf52_repeater`) keep direct messages for up to 4 owner companions while they are away, and hand them over when the owner is heard again with no hop between. Nothing is flooded: messages are kept from traffic the repeater hears anyway, and handed over as zero-hop packets that nobody repeats.

- **What is kept:** only `TXT_MSG` packets addressed to an owner (by the first public key byte, the only one in the packet), still encrypted to the owner; the repeater cannot read them. Channel messages and adverts are not kept. Up to 64 messages in flash (`/mailbox`), so they survive a restart; older than `ttl` hours are dropped; when full, the oldest goes.
- **Only while away:** an owner heard with no hop (its advert, or a flood packet it just sent) in the last `away` minutes is present, and nothing is kept for it. A flood message the owner answered with a PATH reply in the next 2 minutes is dropped as delivered. A sender's automatic retries (same sender, same length, within 30 s) are kept once.
- **Hand-over:** on a zero-hop advert from the owner (Quick menu > Send advert on the Faketec), the kept messages go out one every 2.5 s as zero-hop direct packets. The owner ACKs them as usual; a sender that is still on the mesh gets the late ACK, and the Faketec UI turns its "not delivered" into delivered.
- **Shown once:** the companion firmware drops a direct message it already showed (same sender and timestamp, last 32), so a kept copy or a retry is not shown twice; the sender still gets its ACK.
- **CLI** (serial or remote admin): `mailbox` (state), `mailbox on|off`, `mailbox learn` (the next companion advert with no hop in 2 minutes becomes an owner), `mailbox add <64-hex key>`, `mailbox del <key prefix>`, `mailbox list`, `mailbox ttl <hours>` (default 72), `mailbox away <minutes>` (default 10), `mailbox clear`.

### Fixes to upstream MeshCore

These are in shared code and apply to every board.

- **Crash on short frames from the app:** `CMD_SEND_CHANNEL_TXT_MSG` shorter than its 7-byte header made the text length negative and `sendGroupMessage()` copied ~4 GB. All command handlers that read fixed-size fields now check the frame length first, instead of reading stale bytes from the previous frame; `REBOOT` / `FACTORY_RESET` could previously match text left over from an earlier frame. `SEND_RAW_DATA` path length and `SET_DEFAULT_FLOOD_SCOPE` name scan are bounded.
- **Malformed radio packets:** a `PAYLOAD_TYPE_PATH` whose path runs past the data no longer underflows the extra length and overruns `out_frame`; `TRACE` packets need tag, auth and flags before the hop offset is computed; contact responses are clamped to the frame size.
- **Invalid path lengths:** an invalid `out_path_len` from the app or from `/contacts3` is treated as unknown (flood) instead of producing malformed direct packets.
- **nRF52 BLE RX race:** the RX queue was shifted in `loop()` while the Bluefruit callback appended to it without locking, so frames could be lost or duplicated. It is now a single-producer / single-consumer ring; connect / disconnect request a flush that `loop()` performs.
- **Atomic saves on nRF52:** contacts, channels and prefs are written to a temp file and renamed, so a reset or brown-out during a save no longer corrupts them (falls back to an in-place write when the filesystem has no room for a second copy).
- **Boot hang on the I2C bus:** the nRF52 Wire driver has no timeout, so a missing or stuck device could hang boot. New opt-in flags `-D DISABLE_RTC_AUTODISCOVERY` and `-D DISABLE_ENV_SENSORS` skip the RTC probes and the 112-address sensor scan.
- **Display left on after power-off:** `ui-new` and `ui-tiny` put a non-e-ink display to sleep before SYSTEMOFF, so the OLED no longer drains the battery after hibernate or low-battery shutdown.
- **STM32 builds:** `[stm32_base]` pins the STM32duino framework, because the new 3.0 release breaks RadioLib's Arduino HAL and drops `ltoa()`.

### New shared components

- `src/helpers/ui/QuadratureRotaryInput`: interrupt-driven quadrature decoder with 2 or 4 transitions per detent, resync at the rest position, runtime direction and steps; ISR-safe on ESP32.
- `src/helpers/ui/SSD1306Display` + `CyrillicFont5x8.h`: UTF-8 Cyrillic rendering (`-D DISPLAY_CYRILLIC`); characters the font cannot draw show as a block.
- `src/helpers/ui/GenericVibration`: PWM drive and multi-pulse patterns.
- `src/helpers/ui/MomentaryButton::setDebounce()`: debounce without the multi-click delay, so presses register at once.
- `AbstractUITask::msgSource()`: optional hook telling a UI whether the next message came from a contact or a channel (no-op for the other UIs).
- `DataStore::loadSmallFile()` / `saveSmallFile()`: small UI-owned files with the same atomic save.

### Builds and CI

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
