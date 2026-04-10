#pragma once

// ─── Button Pins (INPUT_PULLUP, pressed = LOW) ────────────────────────────────
// NOTE: GPIO34 and GPIO35 are INPUT-ONLY on ESP32 — no internal pull-up.
//       Add external 10kΩ pull-up resistors to 3.3V on PIN_BTN_UP and
//       PIN_BTN_DOWN, or remap them to GPIO12/GPIO13 in this file.
#define PIN_BTN_UP      34
#define PIN_BTN_DOWN    35
#define PIN_BTN_LEFT    32
#define PIN_BTN_RIGHT   33
#define PIN_BTN_SELECT  25
#define PIN_BTN_BACK    26
#define PIN_BTN_MENU    27

// ─── Display (I2C) ────────────────────────────────────────────────────────────
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_ADDR       0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// ─── SD Card (SPI) ───────────────────────────────────────────────────────────
#define SD_CS           5
#define SD_MOSI         23
#define SD_MISO         19
#define SD_SCK          18

// ─── Misc ─────────────────────────────────────────────────────────────────────
#define BUILTIN_LED     2
#define KERNEL_TICK_MS  10

// ─── Input timing (ms) — typed as uint32_t-compatible literals ───────────────
#define INPUT_DEBOUNCE_MS       ((uint32_t)20)
#define INPUT_HOLD_INITIAL_MS   ((uint32_t)300)
#define INPUT_HOLD_FAST_MS      ((uint32_t)800)
#define INPUT_REPEAT_NORMAL_MS  ((uint32_t)150)
#define INPUT_REPEAT_FAST_MS    ((uint32_t)70)
#define INPUT_QUEUE_SIZE        10

// ─── Native app launcher ──────────────────────────────────────────────────────
#define MAX_APPS        16
