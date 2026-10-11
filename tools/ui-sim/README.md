# Encoder UI screen simulator

Compiles the real `examples/companion_radio/ui-encoder/UITask.cpp` for the host with stub
Arduino/mesh classes and plays a scripted scenario (standby cards, messages, sending with undo,
ACK states, contacts, keyboard, settings, hibernate, CLI rescue) in Bulgarian and English.
Every step is written as a 4x scaled PNG of the 128x64 OLED, with captions in `manifest.tsv`.

    sh tools/ui-sim/build.sh            # screens go to tools/ui-sim/out
    sh tools/ui-sim/build.sh /tmp/shots # or to another folder

Needs only a C++17 compiler. CI runs it in the unit test workflow and uploads the screens
as the `ui-screens` artifact, so a UI change can be reviewed without hardware.
