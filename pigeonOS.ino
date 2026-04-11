/*
 * ██████╗ ██╗ ██████╗ ███████╗ ██████╗ ███╗   ██╗ ██████╗ ███████╗
 * ██╔══██╗██║██╔════╝ ██╔════╝██╔═══██╗████╗  ██║██╔═══██╗██╔════╝
 * ██████╔╝██║██║  ███╗█████╗  ██║   ██║██╔██╗ ██║██║   ██║███████╗
 * ██╔═══╝ ██║██║   ██║██╔══╝  ██║   ██║██║╚██╗██║██║   ██║╚════██║
 * ██║     ██║╚██████╔╝███████╗╚██████╔╝██║ ╚████║╚██████╔╝███████║
 * ╚═╝     ╚═╝ ╚═════╝ ╚══════╝ ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝ ╚══════╝
 *
 * PigeonOS v1.0 — Lightweight Embedded OS for DOIT ESP32 DEVKIT V1
 * Target  : ESP32 (Xtensa LX6, 240 MHz, 520KB SRAM, 4MB Flash)
 * Display : SH1106 128×64 OLED (I2C, SDA=21, SCL=22)
 * SD Card : SPI (MOSI=23, MISO=19, CLK=18, CS=5)
 * Buttons : 7× GPIO, active LOW with internal pull-up
 *
 * Required libraries (install via Arduino Library Manager):
 *   • Adafruit SH110X        (OLED driver)
 *   • Adafruit GFX Library   (graphics primitives)
 *   • SD (built-in)          (SD card)
 *   • WiFi (built-in ESP32)
 *   • BLE (built-in ESP32)
 * ─────────────────────────────────────────────────────────────────
 */

// ═══════════════════════════════════════════════════════════════════
//  INCLUDES
// ═══════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ═══════════════════════════════════════════════════════════════════
//  HARDWARE CONFIGURATION
// ═══════════════════════════════════════════════════════════════════

// OLED Display (I2C)
#define SCREEN_W   128
#define SCREEN_H    64
#define OLED_RESET  -1   // No physical reset pin; -1 uses internal reset
#define OLED_ADDR  0x3C  // Try 0x3D if display stays blank

// SD Card (SPI) — NOTE: GPIO5 is safe for CS on DEVKIT V1
#define SD_CS_PIN 5

// ── Button GPIO Map ───────────────────────────────────────────────
//  Safe pins: avoid 0,2,6-11,12,15 (boot), 34-39 (input-only OK
//  but avoid as pull-up unreliable). Shared SPI pins excluded below.
//  SD uses: MOSI=23, MISO=19, CLK=18, CS=5
//  I2C uses: SDA=21, SCL=22
//  Remaining safe: 16,17,25,26,27,32,33,34,35,36,39 (last 4 input-only)
#define PIN_BTN_UP 25
#define PIN_BTN_DOWN 26
#define PIN_BTN_LEFT 27
#define PIN_BTN_RIGHT 16
#define PIN_BTN_SELECT 17
#define PIN_BTN_BACK 32
#define PIN_BTN_MENU 33

// ═══════════════════════════════════════════════════════════════════
//  BUTTON ABSTRACTION
// ═══════════════════════════════════════════════════════════════════

enum Button : uint8_t {
  BTN_UP = 0,
  BTN_DOWN,
  BTN_LEFT,
  BTN_RIGHT,
  BTN_SELECT,
  BTN_BACK,
  BTN_MENU,
  BTN_COUNT
};

#define DEBOUNCE_MS 50
#define HOLD_MS 600

struct BtnState {
  bool confirmed;  // debounced logical state
  bool lastRaw;
  bool pressed;   // single-tick on press edge
  bool released;  // single-tick on release edge
  bool held;      // sustained after HOLD_MS
  uint32_t pressTime;
  uint32_t debounceTime;
};

static const uint8_t BTN_PINS[BTN_COUNT] = {
  PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT,
  PIN_BTN_SELECT, PIN_BTN_BACK, PIN_BTN_MENU
};
static BtnState btnState[BTN_COUNT];

// ── InputManager ─────────────────────────────────────────────────
namespace InputManager {

void begin() {
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    btnState[i] = { false, false, false, false, false, 0, 0 };
  }
}

void update() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    BtnState& s = btnState[i];
    s.pressed = false;
    s.released = false;

    bool raw = (digitalRead(BTN_PINS[i]) == LOW);  // active LOW

    if (raw != s.lastRaw) {
      s.debounceTime = now;
      s.lastRaw = raw;
    }

    if ((now - s.debounceTime) >= DEBOUNCE_MS) {
      bool prev = s.confirmed;
      s.confirmed = raw;
      if (s.confirmed && !prev) {
        s.pressed = true;
        s.pressTime = now;
        s.held = false;
      } else if (!s.confirmed && prev) {
        s.released = true;
        s.held = false;
      }
      if (s.confirmed && !s.pressed && (now - s.pressTime) >= HOLD_MS) {
        s.held = true;
      }
    }
  }
}

bool isPressed(Button b) {
  return btnState[b].pressed;
}
bool isHeld(Button b) {
  return btnState[b].held;
}
bool isReleased(Button b) {
  return btnState[b].released;
}
bool isDown(Button b) {
  return btnState[b].confirmed;
}

// Blocking wait — returns first pressed button (or BTN_COUNT on timeout)
Button waitForPress(uint32_t timeoutMs = 0) {
  uint32_t start = millis();
  while (true) {
    update();
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
      if (btnState[i].pressed) return (Button)i;
    }
    if (timeoutMs && (millis() - start) >= timeoutMs)
      return BTN_COUNT;
    yield();
  }
}

}  // namespace InputManager

// ═══════════════════════════════════════════════════════════════════
//  DISPLAY / GUI LAYER
// ═══════════════════════════════════════════════════════════════════

Adafruit_SH1106G display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);

namespace GUI {

// ── Primitives ───────────────────────────────────────────────────
void clear() {
  display.clearDisplay();
}

void show() {
  display.display();
}

void text(int16_t x, int16_t y, const char* str,
          uint8_t size = 1, bool inverted = false) {
  display.setTextSize(size);
  if (inverted) {
    int16_t bx, by;
    uint16_t bw, bh;
    display.getTextBounds(str, x, y, &bx, &by, &bw, &bh);
    display.fillRect(bx - 1, by - 1, bw + 2, bh + 2, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    display.setTextColor(SH110X_WHITE);
  }
  display.setCursor(x, y);
  display.print(str);
  // Always restore defaults so subsequent draws start clean
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
}

void textInt(int16_t x, int16_t y, int32_t val, uint8_t size = 1) {
  char buf[16];
  itoa(val, buf, 10);
  text(x, y, buf, size);
}

void hline(int16_t y) {
  display.drawFastHLine(0, y, SCREEN_W, SH110X_WHITE);
}

void rect(int16_t x, int16_t y, int16_t w, int16_t h, bool fill = false) {
  if (fill) display.fillRect(x, y, w, h, SH110X_WHITE);
  else display.drawRect(x, y, w, h, SH110X_WHITE);
}

// ── Header bar with title ─────────────────────────────────────────
void header(const char* title) {
  display.fillRect(0, 0, SCREEN_W, 11, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setTextSize(1);
  display.setCursor(3, 2);
  display.print(title);
  display.setTextColor(SH110X_WHITE);
  display.drawFastHLine(0, 11, SCREEN_W, SH110X_WHITE);
}

// ── Vertical scrollable menu ──────────────────────────────────────
//    items    : array of c-strings
//    count    : number of items
//    selected : currently highlighted index (modified in place)
//    Returns  : true if SELECT was pressed
bool menu(const char** items, uint8_t count, uint8_t& selected,
          const char* title = nullptr, uint8_t* scrollTopPtr = nullptr) {
  const uint8_t ROW_H   = 11;
  const uint8_t Y_START = title ? 13 : 2;
  const uint8_t VISIBLE = (SCREEN_H - Y_START) / ROW_H;

  // Each caller owns its own scrollTop via pointer; fallback to local static
  static uint8_t _localScroll = 0;
  uint8_t& scrollTop = scrollTopPtr ? *scrollTopPtr : _localScroll;

  // Scroll window: keep selected visible
  if (selected < scrollTop) scrollTop = selected;
  if (selected >= scrollTop + VISIBLE) scrollTop = selected - VISIBLE + 1;

  clear();
  if (title) header(title);

  for (uint8_t i = 0; i < VISIBLE; i++) {
    uint8_t idx = scrollTop + i;
    if (idx >= count) break;
    bool sel = (idx == selected);
    int16_t y = Y_START + i * ROW_H;
    if (sel) {
      display.fillRoundRect(0, y - 1, SCREEN_W - 4, ROW_H, 2, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    display.setTextSize(1);
    display.setCursor(4, y + 1);
    display.print(items[idx]);
  }
  display.setTextColor(SH110X_WHITE);

  // Scrollbar
  if (count > VISIBLE) {
    uint8_t barH = max(4, (int)((SCREEN_H - Y_START) * VISIBLE / count));
    uint8_t barY = Y_START + (SCREEN_H - Y_START - barH) * scrollTop / max(1, count - VISIBLE);
    display.drawFastVLine(SCREEN_W - 2, Y_START, SCREEN_H - Y_START, SH110X_WHITE);
    display.fillRect(SCREEN_W - 3, barY, 3, barH, SH110X_WHITE);
  }

  show();

  InputManager::update();
  if (InputManager::isPressed(BTN_UP) && selected > 0) {
    selected--;
    scrollTop = 0;
  }
  if (InputManager::isPressed(BTN_DOWN) && selected < count - 1) { selected++; }
  if (InputManager::isPressed(BTN_SELECT)) return true;
  return false;
}

// ── Simple message dialog ─────────────────────────────────────────
void msgBox(const char* title, const char* msg) {
  clear();
  // Outer rounded border
  display.drawRoundRect(2, 2, SCREEN_W - 4, SCREEN_H - 4, 4, SH110X_WHITE);
  // Title bar filled
  display.fillRoundRect(2, 2, SCREEN_W - 4, 13, 4, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setTextSize(1);
  display.setCursor(6, 4);
  display.print(title);
  // Separator
  display.setTextColor(SH110X_WHITE);
  display.drawFastHLine(2, 15, SCREEN_W - 4, SH110X_WHITE);
  // Message body
  text(6, 20, msg);
  // Footer
  display.drawFastHLine(2, 52, SCREEN_W - 4, SH110X_WHITE);
  text(24, 55, "[SELECT] OK");
  show();
  InputManager::waitForPress(0);  // wait for any key
}

// ── Progress bar ─────────────────────────────────────────────────
void progressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                 uint8_t percent) {
  display.drawRect(x, y, w, h, SH110X_WHITE);
  int16_t fill = (int32_t)percent * (w - 2) / 100;
  if (fill > 0) display.fillRect(x + 1, y + 1, fill, h - 2, SH110X_WHITE);
}

}  // namespace GUI

// ═══════════════════════════════════════════════════════════════════
//  STORAGE LAYER (SD CARD)
// ═══════════════════════════════════════════════════════════════════

namespace Storage {

static bool _mounted = false;

bool begin() {
  _mounted = SD.begin(SD_CS_PIN);
  return _mounted;
}

bool mounted() {
  return _mounted;
}

bool writeFile(const char* path, const char* data) {
  if (!_mounted) return false;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  f.print(data);
  f.close();
  return true;
}

// Reads up to maxLen-1 bytes into buf; returns bytes read
size_t readFile(const char* path, char* buf, size_t maxLen) {
  if (!_mounted) return 0;
  File f = SD.open(path);
  if (!f) return 0;
  size_t n = f.readBytes(buf, maxLen - 1);
  buf[n] = '\0';
  f.close();
  return n;
}

// Calls cb(name, isDir) for each entry in root dir
void listDir(const char* path,
             void (*cb)(const char* name, bool isDir)) {
  if (!_mounted) return;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  File entry;
  while ((entry = dir.openNextFile())) {
    cb(entry.name(), entry.isDirectory());
    entry.close();
  }
  dir.close();
}

bool deleteFile(const char* path) {
  if (!_mounted) return false;
  return SD.remove(path);
}

uint64_t totalBytes() {
  return _mounted ? SD.totalBytes() : 0;
}
uint64_t usedBytes() {
  return _mounted ? SD.usedBytes() : 0;
}

}  // namespace Storage

// ═══════════════════════════════════════════════════════════════════
//  APP BASE CLASS
// ═══════════════════════════════════════════════════════════════════

class App {
public:
  virtual void setup() = 0;  // called once on launch
  virtual void loop() = 0;   // called repeatedly while active
  virtual ~App() {}
};

// ═══════════════════════════════════════════════════════════════════
//  APP MANAGER
// ═══════════════════════════════════════════════════════════════════

namespace AppManager {

#define MAX_APPS 16

struct AppEntry {
  const char* name;
  App* (*factory)();  // factory function to avoid static allocation
};

static AppEntry registry[MAX_APPS];
static uint8_t appCount = 0;
static App* current = nullptr;
static int8_t pendingIdx = -1;  // -1 = no pending switch

void registerApp(const char* name, App* (*factory)()) {
  if (appCount >= MAX_APPS) return;
  registry[appCount++] = { name, factory };
}

void switchTo(uint8_t idx) {
  if (idx >= appCount) return;
  pendingIdx = idx;
}

// Must be called from main loop (not inside another app's loop)
void _applySwitch() {
  if (pendingIdx < 0) return;
  if (current) {
    delete current;
    current = nullptr;
  }
  current = registry[pendingIdx].factory();
  if (current) current->setup();
  pendingIdx = -1;
}

void tick() {
  _applySwitch();
  if (current) current->loop();
}

uint8_t count() {
  return appCount;
}
const char* name(uint8_t i) {
  return i < appCount ? registry[i].name : "";
}

}  // namespace AppManager

// ═══════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════
//   A P P L I C A T I O N S
// ═══════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────
// [LAUNCHER] — Main app grid / home screen
// ─────────────────────────────────────────────────────────────────
class LauncherApp : public App {
  uint8_t _sel = 0;
  uint8_t _scrollTop = 0;
public:
  void setup() override {
    _sel = 0;
    _scrollTop = 0;
  }
  void loop() override {
    // Build name list dynamically (skip index 0 = self to avoid recursion)
    static const char* names[MAX_APPS];
    uint8_t count = AppManager::count();
    uint8_t displayCount = count - 1;  // skip index 0 (Launcher itself)
    for (uint8_t i = 0; i < displayCount; i++)
      names[i] = AppManager::name(i + 1);

    // Draw custom launcher with icons
    const uint8_t ROW_H   = 11;
    const uint8_t Y_START = 13;
    const uint8_t VISIBLE = (SCREEN_H - Y_START) / ROW_H;

    if (_sel < _scrollTop) _scrollTop = _sel;
    if (_sel >= _scrollTop + VISIBLE) _scrollTop = _sel - VISIBLE + 1;

    GUI::clear();
    GUI::header("PigeonOS  \x10 SELECT");  // \x10 = right arrow in CP437

    for (uint8_t i = 0; i < VISIBLE; i++) {
      uint8_t idx = _scrollTop + i;
      if (idx >= displayCount) break;
      bool sel = (idx == _sel);
      int16_t y = Y_START + i * ROW_H;
      if (sel) {
        display.fillRoundRect(0, y, SCREEN_W - 4, ROW_H, 2, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
        display.setCursor(4, y + 2);
        display.print("\x10 ");  // arrow prefix for selected
      } else {
        display.setTextColor(SH110X_WHITE);
        display.setCursor(4, y + 2);
        display.print("  ");
      }
      display.setTextSize(1);
      display.print(names[idx]);
    }
    display.setTextColor(SH110X_WHITE);

    // Scrollbar
    if (displayCount > VISIBLE) {
      uint8_t barH = max(4, (int)((SCREEN_H - Y_START) * VISIBLE / displayCount));
      uint8_t barY = Y_START + (SCREEN_H - Y_START - barH) * _scrollTop / max(1, displayCount - VISIBLE);
      display.drawFastVLine(SCREEN_W - 2, Y_START, SCREEN_H - Y_START, SH110X_WHITE);
      display.fillRect(SCREEN_W - 3, barY, 3, barH, SH110X_WHITE);
    }

    GUI::show();

    InputManager::update();
    if (InputManager::isPressed(BTN_UP)   && _sel > 0) _sel--;
    if (InputManager::isPressed(BTN_DOWN) && _sel < displayCount - 1) _sel++;
    if (InputManager::isPressed(BTN_SELECT)) {
      AppManager::switchTo(_sel + 1);  // +1 because we skip Launcher at 0
    }
    if (InputManager::isPressed(BTN_BACK)) _sel = 0;
  }
};

// ─────────────────────────────────────────────────────────────────
// [SETTINGS]
// ─────────────────────────────────────────────────────────────────
class SettingsApp : public App {
  uint8_t _sel = 0;
  uint8_t _scrollTop = 0;
  static const char* items[];
  static const uint8_t ITEM_COUNT = 4;
public:
  void setup() override {
    _sel = 0;
    _scrollTop = 0;
  }
  void loop() override {
    if (GUI::menu(items, ITEM_COUNT, _sel, "Settings", &_scrollTop)) {
      switch (_sel) {
        case 0:  // Display info
          GUI::msgBox("Display", "128x64 SH1106 I2C");
          break;
        case 1:  // CPU info
          {
            char buf[32];
            snprintf(buf, sizeof(buf), "CPU: %d MHz", getCpuFrequencyMhz());
            GUI::msgBox("CPU", buf);
          }
          break;
        case 2:  // Free RAM
          {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d bytes free", (int)ESP.getFreeHeap());
            GUI::msgBox("Memory", buf);
          }
          break;
        case 3:  // Back
          AppManager::switchTo(0);
          return;
      }
    }
    if (InputManager::isPressed(BTN_BACK)) AppManager::switchTo(0);
  }
};
const char* SettingsApp::items[] = { "Display info", "CPU info", "Free RAM", "< Back" };

// ─────────────────────────────────────────────────────────────────
// [FILE MANAGER]
// ─────────────────────────────────────────────────────────────────
#define FM_MAX_FILES 16
#define FM_NAME_LEN 20

class FileManagerApp : public App {
  char _names[FM_MAX_FILES][FM_NAME_LEN];
  bool _isDir[FM_MAX_FILES];
  uint8_t _count = 0;
  uint8_t _sel = 0;

  static FileManagerApp* _inst;
  static void _enumCb(const char* name, bool isDir) {
    if (!_inst || _inst->_count >= FM_MAX_FILES) return;
    strncpy(_inst->_names[_inst->_count], name, FM_NAME_LEN - 1);
    _inst->_names[_inst->_count][FM_NAME_LEN - 1] = '\0';
    _inst->_isDir[_inst->_count] = isDir;
    _inst->_count++;
  }

  void refresh() {
    _count = 0;
    _sel = 0;
    _inst = this;
    Storage::listDir("/", _enumCb);
  }

public:
  void setup() override {
    if (!Storage::mounted()) {
      GUI::msgBox("File Mgr", "No SD card!");
      AppManager::switchTo(0);
      return;
    }
    refresh();
  }

  void loop() override {
    if (_count == 0) {
      GUI::clear();
      GUI::header("File Manager");
      GUI::text(2, 20, "SD empty or err");
      GUI::text(2, 54, "[BACK] home");
      GUI::show();
      InputManager::update();
      if (InputManager::isPressed(BTN_BACK)) {
        AppManager::switchTo(0);
        return;
      }
      return;
    }

    // Build pointer array for GUI::menu
    static const char* ptrs[FM_MAX_FILES];
    static uint8_t fmScroll = 0;
    for (uint8_t i = 0; i < _count; i++) ptrs[i] = _names[i];

    if (GUI::menu(ptrs, _count, _sel, "Files", &fmScroll)) {
      // SELECT on a file: show size
      if (!_isDir[_sel]) {
        char path[FM_NAME_LEN + 2];
        snprintf(path, sizeof(path), "/%s", _names[_sel]);
        File f = SD.open(path);
        if (f) {
          char info[32];
          snprintf(info, sizeof(info), "%lu bytes", (unsigned long)f.size());
          f.close();
          GUI::msgBox(_names[_sel], info);
        }
      }
    }
    if (InputManager::isPressed(BTN_BACK)) AppManager::switchTo(0);
    if (InputManager::isPressed(BTN_MENU)) refresh();
  }
};
FileManagerApp* FileManagerApp::_inst = nullptr;

// ─────────────────────────────────────────────────────────────────
// [SYSTEM MONITOR]
// ─────────────────────────────────────────────────────────────────
class SysMonitorApp : public App {
  uint32_t _lastUpdate = 0;
  uint32_t _loopCount = 0;
  uint32_t _fps = 0;
  uint32_t _fpsTimer = 0;
  uint32_t _fpsCount = 0;

public:
  void setup() override {
    _lastUpdate = 0;
    _loopCount = 0;
    _fps = 0;
  }
  void loop() override {
    _loopCount++;
    _fpsCount++;
    uint32_t now = millis();

    if (now - _fpsTimer >= 1000) {
      _fps = _fpsCount;
      _fpsCount = 0;
      _fpsTimer = now;
    }

    if (now - _lastUpdate < 250) {
      InputManager::update();
      if (InputManager::isPressed(BTN_BACK)) AppManager::switchTo(0);
      return;
    }
    _lastUpdate = now;

    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    uint8_t heapPct = (uint8_t)(100 - (uint32_t)freeHeap * 100 / totalHeap);
    uint32_t uptime = now / 1000;

    GUI::clear();
    GUI::header("System Monitor");

    char buf[32];
    // Heap bar
    snprintf(buf, sizeof(buf), "RAM %dK/%dK", (int)(freeHeap / 1024), (int)(totalHeap / 1024));
    GUI::text(0, 13, buf);
    GUI::progressBar(0, 23, SCREEN_W, 5, heapPct);

    // CPU freq
    snprintf(buf, sizeof(buf), "CPU: %d MHz", getCpuFrequencyMhz());
    GUI::text(0, 31, buf);

    // Uptime
    snprintf(buf, sizeof(buf), "Up: %02d:%02d:%02d",
             (int)(uptime / 3600), (int)((uptime % 3600) / 60), (int)(uptime % 60));
    GUI::text(0, 41, buf);

    // FPS / SD
    snprintf(buf, sizeof(buf), "FPS:%-4d SD:%s", (int)_fps, Storage::mounted() ? "OK" : "--");
    GUI::text(0, 51, buf);

    GUI::show();
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) AppManager::switchTo(0);
  }
};

// ═══════════════════════════════════════════════════════════════════
//  GAME: SNAKE
// ═══════════════════════════════════════════════════════════════════
#define SNAKE_COLS 21  // 128/6 ≈ 21 cells wide
#define SNAKE_ROWS 8   // (64-12)/6 = 8 cells (avoid header overlap)
#define SNAKE_CELL 6
#define SNAKE_MAX  (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_DELAY 150
#define SNAKE_Y_OFF 13  // pixel Y offset for game area (below header)

class SnakeApp : public App {
  struct Pt {
    int8_t x, y;
  };
  Pt _body[SNAKE_MAX];
  int16_t _len;
  Pt _dir;
  Pt _food;
  bool _dead;
  int16_t _score;
  uint32_t _lastMove;

  void placeFood() {
    uint8_t attempts = 0;
    do {
      _food = { (int8_t)(random(SNAKE_COLS)), (int8_t)(random(SNAKE_ROWS)) };
      attempts++;
    } while (attempts < 100 && onSnake(_food));
  }

  bool onSnake(Pt p) {
    for (int16_t i = 0; i < _len; i++)
      if (_body[i].x == p.x && _body[i].y == p.y) return true;
    return false;
  }

  void drawCell(int8_t cx, int8_t cy, bool fill = true) {
    int16_t px = cx * SNAKE_CELL;
    int16_t py = SNAKE_Y_OFF + cy * SNAKE_CELL;
    if (fill) display.fillRect(px + 1, py + 1, SNAKE_CELL - 1, SNAKE_CELL - 1, SH110X_WHITE);
    else      display.drawRect(px,     py,     SNAKE_CELL,     SNAKE_CELL,     SH110X_WHITE);
  }

public:
  void setup() override {
    _len = 3;
    _dir = { 1, 0 };
    _dead = false;
    _score = 0;
    _lastMove = 0;
    for (int16_t i = 0; i < _len; i++)
      _body[i] = { (int8_t)(SNAKE_COLS / 2 - i), (int8_t)(SNAKE_ROWS / 2) };
    placeFood();
  }

  void loop() override {
    InputManager::update();

    // Direction input (no reversals)
    if (InputManager::isPressed(BTN_UP) && _dir.y == 0) { _dir = { 0, -1 }; }
    if (InputManager::isPressed(BTN_DOWN) && _dir.y == 0) { _dir = { 0, 1 }; }
    if (InputManager::isPressed(BTN_LEFT) && _dir.x == 0) { _dir = { -1, 0 }; }
    if (InputManager::isPressed(BTN_RIGHT) && _dir.x == 0) { _dir = { 1, 0 }; }
    if (InputManager::isPressed(BTN_BACK)) {
      AppManager::switchTo(0);
      return;
    }

    if (_dead) {
      GUI::clear();
      GUI::header("Snake DEAD");
      GUI::text(20, 25, "Game Over!");
      char buf[16];
      snprintf(buf, sizeof(buf), "Score: %d", _score);
      GUI::text(20, 38, buf);
      GUI::text(4, 52, "SELECT:retry BACK:exit");
      GUI::show();
      if (InputManager::isPressed(BTN_SELECT)) setup();
      return;
    }

    uint32_t now = millis();
    bool moved = false;
    if (now - _lastMove >= SNAKE_DELAY) {
      _lastMove = now;
      Pt head = { (int8_t)(_body[0].x + _dir.x), (int8_t)(_body[0].y + _dir.y) };

      // Wall wrap
      head.x = (head.x + SNAKE_COLS) % SNAKE_COLS;
      head.y = (head.y + SNAKE_ROWS) % SNAKE_ROWS;

      // Self-collision
      if (onSnake(head)) {
        _dead = true;
        return;
      }

      bool ate = (head.x == _food.x && head.y == _food.y);
      if (!ate) _len = min(_len, (int16_t)(SNAKE_MAX - 1));

      // Shift body
      for (int16_t i = min(_len, (int16_t)(SNAKE_MAX - 1)); i > 0; i--)
        _body[i] = _body[i - 1];
      _body[0] = head;

      if (ate) {
        if (_len < SNAKE_MAX) _len++;
        _score++;
        placeFood();
      }
      moved = true;
    }

    // Draw
    GUI::clear();
    GUI::header("Snake");
    char sbuf[10];
    snprintf(sbuf, sizeof(sbuf), " %d", _score);
    GUI::text(100, 1, sbuf);

    for (int16_t i = 0; i < _len; i++) drawCell(_body[i].x, _body[i].y);
    drawCell(_food.x, _food.y, false);  // food = hollow square
    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  GAME: PONG
// ═══════════════════════════════════════════════════════════════════
#define PONG_PADDLE_H  14
#define PONG_PADDLE_W   3
#define PONG_BALL_R     2
#define PONG_Y_MIN     12  // below header (header is now 11px + 1px border)
#define PONG_SPEED_INIT 2

class PongApp : public App {
  float _bx, _by, _bdx, _bdy;
  int16_t _p1y, _p2y;
  uint8_t _score1, _score2;
  uint32_t _lastFrame;

  void resetBall(int dir) {
    _bx = SCREEN_W / 2;
    _by = PONG_Y_MIN + (SCREEN_H - PONG_Y_MIN) / 2;
    _bdx = PONG_SPEED_INIT * dir;
    _bdy = (random(2) ? 1 : -1) * PONG_SPEED_INIT * 0.8f;
  }

  void clampPaddle(int16_t& y) {
    if (y < PONG_Y_MIN) y = PONG_Y_MIN;
    if (y + PONG_PADDLE_H > SCREEN_H) y = SCREEN_H - PONG_PADDLE_H;
  }

public:
  void setup() override {
    _p1y = _p2y = PONG_Y_MIN + (SCREEN_H - PONG_Y_MIN - PONG_PADDLE_H) / 2;
    _score1 = _score2 = 0;
    resetBall(1);
    _lastFrame = 0;
  }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) {
      AppManager::switchTo(0);
      return;
    }

    // Player 1 (left, UP/DOWN)
    if (InputManager::isDown(BTN_UP)) {
      _p1y -= 3;
      clampPaddle(_p1y);
    }
    if (InputManager::isDown(BTN_DOWN)) {
      _p1y += 3;
      clampPaddle(_p1y);
    }

    // Player 2 (right) simple AI
    float mid2 = _p2y + PONG_PADDLE_H / 2;
    if (mid2 < _by - 2) _p2y += 2;
    else if (mid2 > _by + 2) _p2y -= 2;
    clampPaddle(_p2y);

    uint32_t now = millis();
    if (now - _lastFrame >= 30) {
      _lastFrame = now;
      _bx += _bdx;
      _by += _bdy;

      // Top/bottom bounce
      if (_by <= PONG_Y_MIN + PONG_BALL_R) {
        _by = PONG_Y_MIN + PONG_BALL_R;
        _bdy = -_bdy;
      }
      if (_by >= SCREEN_H - PONG_BALL_R) {
        _by = SCREEN_H - PONG_BALL_R;
        _bdy = -_bdy;
      }

      // Paddle 1 (left)
      if (_bx <= PONG_PADDLE_W + PONG_BALL_R + 2 && _by >= _p1y && _by <= _p1y + PONG_PADDLE_H) {
        _bdx = abs(_bdx) * 1.05f;
        _bx = PONG_PADDLE_W + PONG_BALL_R + 3;
      }
      // Paddle 2 (right)
      if (_bx >= SCREEN_W - PONG_PADDLE_W - PONG_BALL_R - 2 && _by >= _p2y && _by <= _p2y + PONG_PADDLE_H) {
        _bdx = -abs(_bdx) * 1.05f;
        _bx = SCREEN_W - PONG_PADDLE_W - PONG_BALL_R - 3;
      }

      // Score
      if (_bx < 0) {
        _score2++;
        resetBall(1);
      }
      if (_bx > SCREEN_W) {
        _score1++;
        resetBall(-1);
      }
    }

    GUI::clear();
    GUI::header("Pong");
    char sbuf[12];
    snprintf(sbuf, sizeof(sbuf), "%d  |  %d", _score1, _score2);
    GUI::text(40, 1, sbuf);

    // Paddles
    display.fillRect(1, _p1y, PONG_PADDLE_W, PONG_PADDLE_H, SH110X_WHITE);
    display.fillRect(SCREEN_W - 1 - PONG_PADDLE_W, _p2y, PONG_PADDLE_W, PONG_PADDLE_H, SH110X_WHITE);

    // Ball
    display.fillCircle((int16_t)_bx, (int16_t)_by, PONG_BALL_R, SH110X_WHITE);

    // Center dashed line
    for (int16_t y = PONG_Y_MIN; y < SCREEN_H; y += 5)
      display.drawPixel(SCREEN_W / 2, y, SH110X_WHITE);

    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  GAME: TIC TAC TOE
// ═══════════════════════════════════════════════════════════════════
class TicTacToeApp : public App {
  int8_t _board[9];  // 0=empty, 1=X, -1=O
  uint8_t _cursor;
  int8_t _turn;  // 1=X, -1=O
  bool _done;

  int8_t checkWin() {
    const uint8_t wins[8][3] = {
      { 0, 1, 2 }, { 3, 4, 5 }, { 6, 7, 8 },  // rows
      { 0, 3, 6 },
      { 1, 4, 7 },
      { 2, 5, 8 },  // cols
      { 0, 4, 8 },
      { 2, 4, 6 }  // diags
    };
    for (auto& w : wins) {
      int8_t s = _board[w[0]] + _board[w[1]] + _board[w[2]];
      if (s == 3 || s == -3) return (s > 0) ? 1 : -1;
    }
    return 0;
  }

  bool boardFull() {
    for (uint8_t i = 0; i < 9; i++)
      if (!_board[i]) return false;
    return true;
  }

  // Simple AI: pick winning move, else block, else center, else random
  uint8_t aiMove() {
    // Try to win
    for (uint8_t i = 0; i < 9; i++) {
      if (_board[i]) continue;
      _board[i] = -1;
      if (checkWin() == -1) {
        _board[i] = 0;
        return i;
      }
      _board[i] = 0;
    }
    // Block
    for (uint8_t i = 0; i < 9; i++) {
      if (_board[i]) continue;
      _board[i] = 1;
      if (checkWin() == 1) {
        _board[i] = 0;
        return i;
      }
      _board[i] = 0;
    }
    if (!_board[4]) return 4;  // center
    // Random
    uint8_t tries = 0;
    uint8_t m;
    do {
      m = random(9);
      tries++;
    } while (_board[m] && tries < 30);
    return m;
  }

  void drawBoard() {
    // 3×3 grid centered: each cell 16px, start x=40, y=13
    const int16_t GX = 40, GY = 13, CS = 16;
    GUI::clear();
    GUI::header("Tic Tac Toe");

    // Grid lines
    display.drawFastVLine(GX + CS, GY, CS * 3, SH110X_WHITE);
    display.drawFastVLine(GX + CS * 2, GY, CS * 3, SH110X_WHITE);
    display.drawFastHLine(GX, GY + CS, CS * 3, SH110X_WHITE);
    display.drawFastHLine(GX, GY + CS * 2, CS * 3, SH110X_WHITE);

    for (uint8_t i = 0; i < 9; i++) {
      int16_t cx = GX + (i % 3) * CS + CS / 2;
      int16_t cy = GY + (i / 3) * CS + CS / 2 - 3;
      if (_board[i] == 1) display.drawChar(cx - 3, cy, 'X', SH110X_WHITE, SH110X_BLACK, 1);
      if (_board[i] == -1) display.drawChar(cx - 3, cy, 'O', SH110X_WHITE, SH110X_BLACK, 1);
      // Cursor
      if (i == _cursor && !_done) {
        int16_t rx = GX + (i % 3) * CS + 1;
        int16_t ry = GY + (i / 3) * CS + 1;
        display.drawRect(rx, ry, CS - 2, CS - 2, SH110X_WHITE);
      }
    }

    int8_t w = checkWin();
    if (w || boardFull()) {
      const char* msg = w == 1 ? "X wins!" : (w == -1 ? "O wins!" : "Draw!");
      GUI::text(0, 57, msg);
      GUI::text(70, 57, "SEL:new");
    } else {
      GUI::text(0, 57, _turn == 1 ? "X turn" : "AI...");
    }
    GUI::show();
  }

public:
  void setup() override {
    memset(_board, 0, sizeof(_board));
    _cursor = 4;
    _turn = 1;
    _done = false;
  }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) {
      AppManager::switchTo(0);
      return;
    }

    if (_done) {
      drawBoard();
      if (InputManager::isPressed(BTN_SELECT)) setup();
      return;
    }

    if (_turn == 1) {
      // Player's turn
      if (InputManager::isPressed(BTN_LEFT) && _cursor % 3 > 0) _cursor--;
      if (InputManager::isPressed(BTN_RIGHT) && _cursor % 3 < 2) _cursor++;
      if (InputManager::isPressed(BTN_UP) && _cursor / 3 > 0) _cursor -= 3;
      if (InputManager::isPressed(BTN_DOWN) && _cursor / 3 < 2) _cursor += 3;
      if (InputManager::isPressed(BTN_SELECT) && !_board[_cursor]) {
        _board[_cursor] = 1;
        if (checkWin() || boardFull()) {
          _done = true;
          drawBoard();
          return;
        }
        _turn = -1;
      }
    } else {
      // AI turn — brief visual delay
      drawBoard();
      delay(400);
      uint8_t m = aiMove();
      _board[m] = -1;
      if (checkWin() || boardFull()) {
        _done = true;
        drawBoard();
        return;
      }
      _turn = 1;
    }

    drawBoard();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  NETWORK: WIFI SCANNER
// ═══════════════════════════════════════════════════════════════════
class WiFiScannerApp : public App {
  int _found = 0;
  uint8_t _sel = 0;
  bool _scanning = false;

public:
  void setup() override {
    _found = 0;
    _sel = 0;
    _scanning = true;
    WiFi.mode(WIFI_STA);
  }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) {
      WiFi.mode(WIFI_OFF);
      AppManager::switchTo(0);
      return;
    }

    if (_scanning) {
      GUI::clear();
      GUI::header("WiFi Scanner");
      GUI::text(10, 28, "Scanning...");
      GUI::show();
      _found = WiFi.scanNetworks();
      _scanning = false;
      _sel = 0;
      return;
    }

    // Refresh scan on MENU
    if (InputManager::isPressed(BTN_MENU)) {
      _scanning = true;
      return;
    }

    if (_found <= 0) {
      GUI::clear();
      GUI::header("WiFi Scanner");
      GUI::text(0, 20, "No networks found");
      GUI::text(0, 54, "MENU:scan BACK:exit");
      GUI::show();
      return;
    }

    // Show list
    const uint8_t VISIBLE = 5;
    static uint8_t scrollTop = 0;
    if (_sel < scrollTop) scrollTop = _sel;
    if (_sel >= scrollTop + VISIBLE) scrollTop = _sel - VISIBLE + 1;

    if (InputManager::isPressed(BTN_UP) && _sel > 0) _sel--;
    if (InputManager::isPressed(BTN_DOWN) && _sel < (uint8_t)(_found - 1)) _sel++;

    GUI::clear();
    GUI::header("WiFi Networks");

    for (uint8_t i = 0; i < VISIBLE; i++) {
      uint8_t idx = scrollTop + i;
      if (idx >= (uint8_t)_found) break;
      bool sel = (idx == _sel);
      int16_t y = 13 + i * 10;
      if (sel) {
        display.fillRect(0, y - 1, SCREEN_W, 10, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      } else {
        display.setTextColor(SH110X_WHITE);
      }
      display.setTextSize(1);
      display.setCursor(2, y);
      display.print(WiFi.SSID(idx).substring(0, 14));
      // RSSI bar
      int rssi = WiFi.RSSI(idx);
      uint8_t bars = (rssi > -50) ? 4 : (rssi > -65) ? 3
                                      : (rssi > -75) ? 2
                                                     : 1;
      for (uint8_t b = 0; b < bars; b++) {
        if (sel) display.fillRect(SCREEN_W - 18 + b * 4, y + 6 - b * 2, 3, b * 2 + 2, SH110X_BLACK);
        else display.fillRect(SCREEN_W - 18 + b * 4, y + 6 - b * 2, 3, b * 2 + 2, SH110X_WHITE);
      }
    }
    display.setTextColor(SH110X_WHITE);

    // Detail of selected
    if (InputManager::isPressed(BTN_SELECT)) {
      char info[32];
      snprintf(info, sizeof(info), "RSSI:%d Ch:%d",
               WiFi.RSSI(_sel), WiFi.channel(_sel));
      GUI::msgBox(WiFi.SSID(_sel).substring(0, 16).c_str(), info);
    }

    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  NETWORK: BLE SCANNER
// ═══════════════════════════════════════════════════════════════════
#define BLE_MAX_DEVS 8
#define BLE_NAME_LEN 18

class BLEScannerApp : public App {
  char _devNames[BLE_MAX_DEVS][BLE_NAME_LEN];
  int _devRSSI[BLE_MAX_DEVS];
  uint8_t _devCount = 0;
  uint8_t _sel = 0;
  bool _scanning = false;
  BLEScan* _scan = nullptr;

public:
  void setup() override {
    _devCount = 0;
    _sel = 0;
    BLEDevice::init("");
    _scan = BLEDevice::getScan();
    _scan->setActiveScan(true);
    _scanning = true;
  }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) {
      BLEDevice::deinit(true);
      AppManager::switchTo(0);
      return;
    }

    if (_scanning) {
      GUI::clear();
      GUI::header("BLE Scanner");
      GUI::text(10, 28, "Scanning 3s...");
      GUI::show();

      BLEScanResults* res = _scan->start(3, false);
      _devCount = 0;
      for (int i = 0; i < res->getCount() && _devCount < BLE_MAX_DEVS; i++) {
        BLEAdvertisedDevice dev = res->getDevice(i);
        String name = dev.haveName() ? dev.getName().c_str() : "Unknown";
        strncpy(_devNames[_devCount], name.substring(0, BLE_NAME_LEN - 1).c_str(), BLE_NAME_LEN - 1);
        _devNames[_devCount][BLE_NAME_LEN - 1] = '\0';
        _devRSSI[_devCount] = dev.getRSSI();
        _devCount++;
      }
      _scan->clearResults();
      _scanning = false;
      _sel = 0;
      return;
    }

    if (InputManager::isPressed(BTN_MENU)) {
      _scanning = true;
      return;
    }

    GUI::clear();
    GUI::header("BLE Devices");

    if (_devCount == 0) {
      GUI::text(0, 22, "No devices found");
      GUI::text(0, 54, "MENU:scan BACK:exit");
    } else {
      const uint8_t VISIBLE = 5;
      static uint8_t scrollTop = 0;
      if (_sel < scrollTop) scrollTop = _sel;
      if ((uint8_t)(_sel) >= scrollTop + VISIBLE) scrollTop = _sel - VISIBLE + 1;

      if (InputManager::isPressed(BTN_UP) && _sel > 0) _sel--;
      if (InputManager::isPressed(BTN_DOWN) && _sel < _devCount - 1) _sel++;

      for (uint8_t i = 0; i < VISIBLE; i++) {
        uint8_t idx = scrollTop + i;
        if (idx >= _devCount) break;
        bool sel = (idx == _sel);
        int16_t y = 13 + i * 10;
        if (sel) {
          display.fillRect(0, y - 1, SCREEN_W, 10, SH110X_WHITE);
          display.setTextColor(SH110X_BLACK);
        } else {
          display.setTextColor(SH110X_WHITE);
        }
        display.setTextSize(1);
        display.setCursor(2, y);
        display.print(_devNames[idx]);
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", _devRSSI[idx]);
        display.setCursor(100, y);
        display.print(rssi);
      }
      display.setTextColor(SH110X_WHITE);
    }
    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  NETWORK: SIGNAL MONITOR (WiFi RSSI live graph)
// ═══════════════════════════════════════════════════════════════════
#define SIG_SAMPLES 100

class SignalMonitorApp : public App {
  int8_t _rssi[SIG_SAMPLES];
  uint8_t _head = 0;
  bool _connected = false;
  uint32_t _lastSample = 0;
  char _ssid[33] = "";

public:
  void setup() override {
    memset(_rssi, -100, sizeof(_rssi));
    _head = 0;
    WiFi.mode(WIFI_STA);
    _connected = (WiFi.status() == WL_CONNECTED);
    if (_connected) strncpy(_ssid, WiFi.SSID().c_str(), 32);
  }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) {
      WiFi.mode(WIFI_OFF);
      AppManager::switchTo(0);
      return;
    }

    uint32_t now = millis();
    if (now - _lastSample >= 500) {
      _lastSample = now;
      _connected = (WiFi.status() == WL_CONNECTED);
      _rssi[_head] = _connected ? (int8_t)WiFi.RSSI() : -100;
      _head = (_head + 1) % SIG_SAMPLES;
    }

    GUI::clear();
    GUI::header("Signal Monitor");

    if (!_connected) {
      GUI::text(0, 22, "Not connected");
      GUI::text(0, 32, "to any WiFi");
    } else {
      GUI::text(0, 12, _ssid);
      // Draw graph: map -100..-30 dBm to 0..height pixels
      const int16_t GY_BOT = 62, GY_TOP = 24, GH = GY_BOT - GY_TOP;
      display.drawFastHLine(0, GY_BOT, SIG_SAMPLES, SH110X_WHITE);
      for (uint8_t i = 0; i < SIG_SAMPLES; i++) {
        uint8_t idx = (_head + i) % SIG_SAMPLES;
        int8_t v = _rssi[idx];
        if (v <= -100) continue;
        int16_t h = (int16_t)(v + 100) * GH / 70;
        if (h < 1) h = 1;
        display.drawFastVLine(i, GY_BOT - h, h, SH110X_WHITE);
      }
      // Current value label
      int8_t cur = _rssi[(_head + SIG_SAMPLES - 1) % SIG_SAMPLES];
      char buf[12];
      snprintf(buf, sizeof(buf), "%d dBm", cur);
      GUI::text(80, 12, buf);
    }
    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  UTILITY: GRAPHING CALCULATOR
// ═══════════════════════════════════════════════════════════════════
// Plots y = sin(x), cos(x), tan(x), x^2 — cycle with SELECT
class GraphCalcApp : public App {
  uint8_t _func = 0;  // 0=sin, 1=cos, 2=tan, 3=x²

  float evalFunc(float x) {
    switch (_func) {
      case 0: return sinf(x);
      case 1: return cosf(x);
      case 2: return tanf(x * 0.3f) * 0.5f;  // scaled
      case 3: return x * x * 0.1f;
    }
    return 0;
  }

  static const char* funcName(uint8_t f) {
    switch (f) {
      case 0: return "y=sin(x)";
      case 1: return "y=cos(x)";
      case 2: return "y=tan(0.3x)";
      case 3: return "y=x^2";
    }
    return "";
  }

public:
  void setup() override {
    _func = 0;
  }
  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) {
      AppManager::switchTo(0);
      return;
    }
    if (InputManager::isPressed(BTN_SELECT) || InputManager::isPressed(BTN_RIGHT))
      _func = (_func + 1) % 4;
    if (InputManager::isPressed(BTN_LEFT) && _func > 0) _func--;

    GUI::clear();
    GUI::header("Graph Calc");
    GUI::text(2, 1, funcName(_func));

    // Plot area: x=0..127, y=13..63 (50px tall)
    const int16_t PY_MID = 13 + 25;
    const int16_t PY_TOP = 13, PY_BOT = 63;
    const float X_RANGE = 6.28f * 2;  // two full periods
    const float Y_SCALE = 22.0f;

    // Axes
    display.drawFastHLine(0, PY_MID, SCREEN_W, SH110X_WHITE);
    display.drawFastVLine(SCREEN_W / 2, PY_TOP, PY_BOT - PY_TOP, SH110X_WHITE);

    // Plot
    for (int16_t px = 0; px < SCREEN_W; px++) {
      float x = ((float)px / SCREEN_W - 0.5f) * X_RANGE;
      float y = evalFunc(x);
      int16_t py = (int16_t)(PY_MID - y * Y_SCALE);
      if (py < PY_TOP || py > PY_BOT) continue;
      display.drawPixel(px, py, SH110X_WHITE);
    }

    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  UTILITY: CHEMISTRY APP
//  Periodic table viewer — navigate with L/R, show element info
// ═══════════════════════════════════════════════════════════════════
struct ChemElement {
  uint8_t num;
  const char* symbol;
  const char* name;
  float mass;
};

static const ChemElement ELEMENTS[] PROGMEM = {
  { 1, "H", "Hydrogen", 1.008f },
  { 2, "He", "Helium", 4.003f },
  { 3, "Li", "Lithium", 6.941f },
  { 4, "Be", "Beryllium", 9.012f },
  { 5, "B", "Boron", 10.811f },
  { 6, "C", "Carbon", 12.011f },
  { 7, "N", "Nitrogen", 14.007f },
  { 8, "O", "Oxygen", 15.999f },
  { 9, "F", "Fluorine", 18.998f },
  { 10, "Ne", "Neon", 20.180f },
  { 11, "Na", "Sodium", 22.990f },
  { 12, "Mg", "Magnesium", 24.305f },
  { 13, "Al", "Aluminum", 26.982f },
  { 14, "Si", "Silicon", 28.086f },
  { 15, "P", "Phosphorus", 30.974f },
  { 16, "S", "Sulfur", 32.065f },
  { 17, "Cl", "Chlorine", 35.453f },
  { 18, "Ar", "Argon", 39.948f },
};
#define CHEM_COUNT (sizeof(ELEMENTS) / sizeof(ELEMENTS[0]))

class ChemApp : public App {
  uint8_t _sel = 0;
public:
  void setup() override {
    _sel = 0;
  }
  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) {
      AppManager::switchTo(0);
      return;
    }
    if (InputManager::isPressed(BTN_LEFT) && _sel > 0) _sel--;
    if (InputManager::isPressed(BTN_RIGHT) && _sel < CHEM_COUNT - 1) _sel++;
    if (InputManager::isPressed(BTN_UP) && _sel >= 8) _sel -= 8;
    if (InputManager::isPressed(BTN_DOWN) && _sel + 8 < CHEM_COUNT) _sel += 8;

    const ChemElement& e = ELEMENTS[_sel];

    GUI::clear();
    GUI::header("Chemistry");

    // Big symbol box
    display.drawRect(4, 13, 42, 40, SH110X_WHITE);
    GUI::text(8, 15, String(e.num).c_str());
    GUI::text(10, 26, e.symbol, 2);

    // Info
    GUI::text(52, 15, e.name);
    char mbuf[16];
    snprintf(mbuf, sizeof(mbuf), "%.3f u", e.mass);
    GUI::text(52, 27, "Mass:");
    GUI::text(52, 37, mbuf);

    // Navigation hint
    char nav[20];
    snprintf(nav, sizeof(nav), "%d/%d L/R:nav", (int)(_sel + 1), (int)CHEM_COUNT);
    GUI::text(0, 56, nav);

    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  UTILITY: LIGHT TOOL (Morse code / torch flash with LED on GPIO2)
//  GPIO2 has on-board LED on DOIT DEVKIT V1
// ═══════════════════════════════════════════════════════════════════
#define LED_PIN 2

class LightToolApp : public App {
  uint8_t _mode = 0;  // 0=off, 1=on, 2=blink, 3=SOS
  uint32_t _lastBlink = 0;
  bool _blinkState = false;
  uint8_t _sosStep = 0;
  uint32_t _sosNext = 0;

  // SOS pattern: 3 short, 3 long, 3 short (ms on, ms off)
  static const uint16_t SOS_ON[];
  static const uint16_t SOS_OFF[];
  static const uint8_t SOS_LEN = 9;

public:
  void setup() override {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    _mode = 0;
    _sosStep = 0;
  }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) {
      digitalWrite(LED_PIN, LOW);
      AppManager::switchTo(0);
      return;
    }

    if (InputManager::isPressed(BTN_UP)) _mode = (_mode + 1) % 4;
    if (InputManager::isPressed(BTN_DOWN) && _mode > 0) _mode--;

    uint32_t now = millis();

    switch (_mode) {
      case 0: digitalWrite(LED_PIN, LOW); break;
      case 1: digitalWrite(LED_PIN, HIGH); break;
      case 2:
        if (now - _lastBlink >= 500) {
          _blinkState = !_blinkState;
          _lastBlink = now;
          digitalWrite(LED_PIN, _blinkState);
        }
        break;
      case 3:  // SOS
        if (now >= _sosNext) {
          _blinkState = !_blinkState;
          digitalWrite(LED_PIN, _blinkState);
          if (_blinkState) {
            _sosNext = now + SOS_ON[_sosStep];
          } else {
            _sosNext = now + SOS_OFF[_sosStep];
            _sosStep = (_sosStep + 1) % SOS_LEN;
          }
        }
        break;
    }

    GUI::clear();
    GUI::header("Light Tool");
    const char* modes[] = { "OFF", "ON (torch)", "Blink", "SOS" };
    GUI::text(20, 24, "Mode:");
    GUI::text(20, 34, modes[_mode], 1, _mode > 0);
    GUI::text(0, 55, "UP/DN:mode BACK:exit");
    GUI::show();
  }
};

const uint16_t LightToolApp::SOS_ON[] = { 200, 200, 200, 600, 600, 600, 200, 200, 200 };
const uint16_t LightToolApp::SOS_OFF[] = { 200, 200, 600, 200, 200, 200, 600, 200, 800 };

// ═══════════════════════════════════════════════════════════════════
//  APP FACTORY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════
App* makeLauncher() {
  return new LauncherApp();
}
App* makeSettings() {
  return new SettingsApp();
}
App* makeFileManager() {
  return new FileManagerApp();
}
App* makeSysMonitor() {
  return new SysMonitorApp();
}
App* makeSnake() {
  return new SnakeApp();
}
App* makePong() {
  return new PongApp();
}
App* makeTicTacToe() {
  return new TicTacToeApp();
}
App* makeWiFiScanner() {
  return new WiFiScannerApp();
}
App* makeBLEScanner() {
  return new BLEScannerApp();
}
App* makeSignalMon() {
  return new SignalMonitorApp();
}
App* makeGraphCalc() {
  return new GraphCalcApp();
}
App* makeChem() {
  return new ChemApp();
}
App* makeLightTool() {
  return new LightToolApp();
}

// ═══════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("PigeonOS v1.0 booting...");

  // ── Display ──────────────────────────────────────────────────
  Wire.begin(21, 22);        // SDA=21, SCL=22
  Wire.setClock(400000);     // 400 kHz Fast-mode for reliable init

  // Try primary address; if it fails, try alternate 0x3D
  bool dispOk = display.begin(OLED_ADDR, true);
  if (!dispOk) {
    dispOk = display.begin(0x3D, true);
  }

  if (!dispOk) {
    Serial.println("SH1106 not found! Check wiring & address.");
    // Halt with LED blink
    pinMode(LED_PIN, OUTPUT);
    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }

  display.cp437(true);          // Use full 256-char CP437 font
  display.setTextWrap(false);   // Prevent text wrap messing up layouts
  display.setTextColor(SH110X_WHITE);
  display.clearDisplay();
  display.display();

  // ── Boot splash ───────────────────────────────────────────────
  display.clearDisplay();
  // Outer border
  display.drawRoundRect(0, 0, SCREEN_W, SCREEN_H, 4, SH110X_WHITE);
  // Top banner
  display.fillRoundRect(0, 0, SCREEN_W, 18, 4, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setTextSize(2);
  display.setCursor(8, 2);
  display.print("PigeonOS");
  // Version & hardware info
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(38, 22);
  display.print("v1.0");
  display.setCursor(10, 34);
  display.print("ESP32 DEVKIT V1");
  // Loading bar outline
  display.drawRect(10, 46, 108, 8, SH110X_WHITE);
  display.display();
  // Animate loading bar
  for (uint8_t i = 0; i <= 104; i += 8) {
    display.fillRect(11, 47, i, 6, SH110X_WHITE);
    display.display();
    delay(40);
  }
  display.setCursor(30, 56);
  display.print("Initializing...");
  display.display();
  delay(400);

  // ── SD Card ───────────────────────────────────────────────────
  if (Storage::begin()) {
    Serial.println("SD card mounted OK");
  } else {
    Serial.println("SD card not found (optional)");
  }

  // ── Input ─────────────────────────────────────────────────────
  InputManager::begin();

  // ── Register apps ─────────────────────────────────────────────
  AppManager::registerApp("Launcher", makeLauncher);
  AppManager::registerApp("Settings", makeSettings);
  AppManager::registerApp("File Manager", makeFileManager);
  AppManager::registerApp("Sys Monitor", makeSysMonitor);
  AppManager::registerApp("Snake", makeSnake);
  AppManager::registerApp("Pong", makePong);
  AppManager::registerApp("Tic Tac Toe", makeTicTacToe);
  AppManager::registerApp("WiFi Scanner", makeWiFiScanner);
  AppManager::registerApp("BLE Scanner", makeBLEScanner);
  AppManager::registerApp("Signal Mon.", makeSignalMon);
  AppManager::registerApp("Graph Calc", makeGraphCalc);
  AppManager::registerApp("Chemistry", makeChem);
  AppManager::registerApp("Light Tool", makeLightTool);

  // ── Launch home (Launcher = index 0) ─────────────────────────
  AppManager::switchTo(0);

  Serial.printf("PigeonOS ready. %d apps registered.\n", AppManager::count());
  Serial.printf("Free heap: %d bytes\n", (int)ESP.getFreeHeap());
}

// ═══════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════════
void loop() {
  InputManager::update();

  // Global MENU button: always return to launcher
  if (InputManager::isPressed(BTN_MENU)) {
    AppManager::switchTo(0);
  }

  AppManager::tick();

  // Yield so RTOS tasks (WiFi/BT) get time
  yield();
}