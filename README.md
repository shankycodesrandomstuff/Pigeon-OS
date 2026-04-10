# PigeonOS

Lightweight modular embedded OS for **ESP32 DEVKIT v1** (Arduino framework).

---

## Quick start

1. Install Arduino libraries (Library Manager):
   - **U8g2** — OLED driver
   - **SD** (built-in ESP32 core)

2. Edit `Config.h` to match your wiring.

3. Compile and flash.

---

## Wiring

```
SH1106 OLED   →  ESP32
  VCC         →  3.3V
  GND         →  GND
  SDA         →  GPIO21
  SCL         →  GPIO22

SD module     →  ESP32
  VCC         →  3.3V
  GND         →  GND
  MOSI        →  GPIO23
  MISO        →  GPIO19
  SCK         →  GPIO18
  CS          →  GPIO5

Buttons (press to GND, other side to ESP32 pin):
  UP          →  GPIO34  ⚠ needs external 10kΩ pull-up to 3.3V
  DOWN        →  GPIO35  ⚠ needs external 10kΩ pull-up to 3.3V
  LEFT        →  GPIO32
  RIGHT       →  GPIO33
  SELECT      →  GPIO25
  BACK        →  GPIO26
  MENU        →  GPIO27
```

> **GPIO34/35 warning:** These pins are input-only on ESP32 and have no
> internal pull-up circuitry. You **must** add external 10 kΩ resistors from
> each pin to 3.3 V, or remap UP/DOWN to different GPIOs in `Config.h`.

---

## Native app model

Apps are compiled native C++ classes implementing the `App` interface:

```cpp
class App {
public:
    virtual void setup() = 0;
    virtual void loop() = 0;
};
```

`AppLoader` contains a static app registry and a launcher menu. At runtime:
- SELECT opens the highlighted app.
- BACK exits the current app and returns to launcher.

---

## File structure

```
PigeonOS/
├── main.ino            ← Kernel (setup + loop)
├── Config.h            ← All pin / timing / size constants
├── InputManager.h/.cpp ← Button polling, debounce, hold-repeat, event queue
├── UIEngine.h/.cpp     ← SH1106 wrapper (U8g2), menu renderer
├── SDManager.h/.cpp    ← SD init state (for data/config usage)
├── App.h               ← Native app interface
├── NativeApps.h/.cpp   ← Built-in native app implementations
└── AppLoader.h/.cpp    ← Launcher UI + native app lifecycle
```
