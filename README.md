# PigeonOS (Single Sketch)

PigeonOS is now merged into a **single Arduino sketch file**: `main.ino`.

## Notes
- Target: ESP32 DEVKIT v1
- Framework: Arduino
- App model: Native C++ apps (`App::setup()` / `App::loop()`)
- No Lua runtime or script loading

## Build
Open `main.ino` in Arduino IDE (or compile with `arduino-cli`) using an ESP32 board profile.
