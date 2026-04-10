# PigeonOS (Single Sketch, Improved UI)

PigeonOS runs as a single Arduino sketch (`main.ino`) with a native C++ app launcher and improved OLED UI (header, list, detail line, footer softkeys).

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

## 7-Button Controls

- **UP / DOWN**: list navigation and vertical controls
- **LEFT / RIGHT**: horizontal controls in games/settings
- **SELECT**: launch app / primary action
- **BACK**: exit app back to launcher
- **MENU**: secondary action (toggle view, reset, refresh, etc.)

Launcher-specific:
- `UP/DOWN`: choose app
- `SELECT`: open app
- `MENU`: toggle compact group view vs full app labels

## Wiring Plan (ESP32 DEVKIT v1)

### OLED (SH1106, I2C)
- VCC → 3.3V
- GND → GND
- SDA → GPIO21
- SCL → GPIO22

### SD (SPI)
- VCC → 3.3V
- GND → GND
- MOSI → GPIO23
- MISO → GPIO19
- SCK → GPIO18
- CS → GPIO5

### Buttons
- UP → GPIO34 *(external 10k pull-up required)*
- DOWN → GPIO35 *(external 10k pull-up required)*
- LEFT → GPIO32
- RIGHT → GPIO33
- SELECT → GPIO25
- BACK → GPIO26
- MENU → GPIO27

> GPIO34/GPIO35 are input-only and do not have internal pull-ups.
