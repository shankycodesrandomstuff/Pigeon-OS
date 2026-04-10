# PigeonOS (Single Sketch)

PigeonOS is packaged as a single sketch: `main.ino`.

## App Suite

```
Apps
├── Core
│   ├── Settings
│   ├── File Manager
│   ├── System Monitor
│   └── Launcher
├── Games
│   ├── Snake
│   ├── Pong
│   └── Tic Tac Toe
├── Network
│   ├── WiFi Scanner
│   ├── BLE Scanner
│   └── Signal Monitor
└── Utilities
    ├── Graphing Calculator
    ├── Chemistry App
    └── Light Tool
```

## 7-Button Control Plan

- **UP**: Navigate up / app-specific increment
- **DOWN**: Navigate down / app-specific decrement
- **LEFT**: Move left / app-specific previous
- **RIGHT**: Move right / app-specific next
- **SELECT**: Open app / confirm / action
- **BACK**: Exit active app and return to launcher
- **MENU**: Secondary action (refresh/reset/scan depending on app)

## Wiring Plan (ESP32 DEVKIT v1)

### SH1106 OLED (I2C)
- VCC → 3.3V
- GND → GND
- SDA → GPIO21
- SCL → GPIO22

### SD Card (SPI)
- VCC → 3.3V
- GND → GND
- MOSI → GPIO23
- MISO → GPIO19
- SCK → GPIO18
- CS → GPIO5

### Buttons (one side to pin, one side to GND)
- UP → GPIO34 *(requires external 10k pull-up to 3.3V)*
- DOWN → GPIO35 *(requires external 10k pull-up to 3.3V)*
- LEFT → GPIO32
- RIGHT → GPIO33
- SELECT → GPIO25
- BACK → GPIO26
- MENU → GPIO27

> GPIO34 and GPIO35 are input-only and do not support internal pull-ups.
