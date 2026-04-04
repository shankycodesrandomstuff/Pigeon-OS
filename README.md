# Pigeon OS (ESP32 / ESP-IDF)

Minimal modular embedded OS prototype for ESP32:

- Lua runtime with custom allocator
- HAL APIs (`gpio.write`, `gpio.read`, `delay`)
- SD card app loading (`/sd/apps/main.lua`)
- Event-driven update loop (`update()` every 50ms)
- Optional `on_event(event)` callback

## Build

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Notes

- Uses SPI SD card pins: CLK=18, MISO=19, MOSI=23, CS=5.
- Place your Lua app at `/sd/apps/main.lua` on the SD card.
