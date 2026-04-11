/*
 * ██████╗ ██╗ ██████╗ ███████╗ ██████╗ ███╗   ██╗ ██████╗ ███████╗
 * ██╔══██╗██║██╔════╝ ██╔════╝██╔═══██╗████╗  ██║██╔═══██╗██╔════╝
 * ██████╔╝██║██║  ███╗█████╗  ██║   ██║██╔██╗ ██║██║   ██║███████╗
 * ██╔═══╝ ██║██║   ██║██╔══╝  ██║   ██║██║╚██╗██║██║   ██║╚════██║
 * ██║     ██║╚██████╔╝███████╗╚██████╔╝██║ ╚████║╚██████╔╝███████║
 * ╚═╝     ╚═╝ ╚═════╝ ╚══════╝ ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝ ╚══════╝
 *
 * PigeonOS v2.0 — Lightweight Embedded OS for DOIT ESP32 DEVKIT V1
 * Target  : ESP32 (Xtensa LX6, 240 MHz, 520KB SRAM, 4MB Flash)
 * Display : SH1106 128×64 OLED (I2C, SDA=21, SCL=22)
 * SD Card : SPI (MOSI=23, MISO=19, CLK=18, CS=5)
 * Buttons : 7× GPIO, active LOW with internal pull-up
 *
 * FIXES v2.0:
 *   • Root cause of frozen sub-menus fixed: GUI::menu() no longer calls
 *     InputManager::update() internally — callers own their update cycle.
 *   • Removed shared static scrollTop from GUI::menu() — all callers pass
 *     their own scrollTop variable.
 *   • PROGMEM removed from ChemElement array (non-POD struct, UB on ESP32).
 *   • All 118 elements added to the periodic table.
 *   • Snake: Normal / Speed / Wall-Death modes.
 *   • Pong:  1-Player / 2-Player (UP/DN left, LEFT/RIGHT right) / Turbo modes.
 *   • Tic-Tac-Toe: vs AI / vs Player 2 / Hard-AI (minimax) modes.
 *   • Boot animation polished with pixel-art pigeon logo.
 *   • Launcher: icon glyphs, animated selection highlight.
 *   • Menu: smooth scrollbar, selection invert, no double-update bug.
 *
 * Required libraries (install via Arduino Library Manager):
 *   • Adafruit SH110X        (OLED driver)
 *   • Adafruit GFX Library   (graphics primitives)
 *   • SD (built-in)
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
#define SCREEN_W   128
#define SCREEN_H    64
#define OLED_RESET  -1
#define OLED_ADDR  0x3C

#define SD_CS_PIN 5
#define LED_PIN   2

#define PIN_BTN_UP     25
#define PIN_BTN_DOWN   26
#define PIN_BTN_LEFT   27
#define PIN_BTN_RIGHT  16
#define PIN_BTN_SELECT 17
#define PIN_BTN_BACK   32
#define PIN_BTN_MENU   33

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
#define HOLD_MS     600

struct BtnState {
  bool confirmed;
  bool lastRaw;
  bool pressed;
  bool released;
  bool held;
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
    s.pressed  = false;
    s.released = false;

    bool raw = (digitalRead(BTN_PINS[i]) == LOW);

    if (raw != s.lastRaw) {
      s.debounceTime = now;
      s.lastRaw = raw;
    }

    if ((now - s.debounceTime) >= DEBOUNCE_MS) {
      bool prev = s.confirmed;
      s.confirmed = raw;
      if (s.confirmed && !prev) {
        s.pressed   = true;
        s.pressTime = now;
        s.held      = false;
      } else if (!s.confirmed && prev) {
        s.released = true;
        s.held     = false;
      }
      if (s.confirmed && !s.pressed && (now - s.pressTime) >= HOLD_MS) {
        s.held = true;
      }
    }
  }
}

bool isPressed (Button b) { return btnState[b].pressed;   }
bool isHeld    (Button b) { return btnState[b].held;       }
bool isReleased(Button b) { return btnState[b].released;   }
bool isDown    (Button b) { return btnState[b].confirmed;  }

// Blocking wait — InputManager::update() is called internally here only.
// Apps must NOT call update() inside waitForPress loops — use this instead.
Button waitForPress(uint32_t timeoutMs = 0) {
  uint32_t start = millis();
  while (true) {
    update();
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
      if (btnState[i].pressed) return (Button)i;
    }
    if (timeoutMs && (millis() - start) >= timeoutMs) return BTN_COUNT;
    yield();
  }
}

}  // namespace InputManager

// ═══════════════════════════════════════════════════════════════════
//  DISPLAY / GUI LAYER
// ═══════════════════════════════════════════════════════════════════
Adafruit_SH1106G display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);

namespace GUI {

void clear() { display.clearDisplay(); }
void show()  { display.display();      }

void text(int16_t x, int16_t y, const char* str,
          uint8_t size = 1, bool inverted = false) {
  display.setTextSize(size);
  if (inverted) {
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(str, x, y, &bx, &by, &bw, &bh);
    display.fillRect(bx - 1, by - 1, bw + 2, bh + 2, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    display.setTextColor(SH110X_WHITE);
  }
  display.setCursor(x, y);
  display.print(str);
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
}

void hline(int16_t y) {
  display.drawFastHLine(0, y, SCREEN_W, SH110X_WHITE);
}

void rect(int16_t x, int16_t y, int16_t w, int16_t h, bool fill = false) {
  if (fill) display.fillRect(x, y, w, h, SH110X_WHITE);
  else      display.drawRect(x, y, w, h, SH110X_WHITE);
}

void header(const char* title) {
  display.fillRect(0, 0, SCREEN_W, 11, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setTextSize(1);
  display.setCursor(3, 2);
  display.print(title);
  display.setTextColor(SH110X_WHITE);
  display.drawFastHLine(0, 11, SCREEN_W, SH110X_WHITE);
}

// ── FIXED: menu() NO LONGER calls InputManager::update() internally.
//    The caller is fully responsible for calling update() each frame.
//    scrollTopPtr must always be provided (no more shared local static).
//    Returns true on the frame that SELECT is pressed.
bool menu(const char** items, uint8_t count, uint8_t& selected,
          const char* title, uint8_t& scrollTop) {
  const uint8_t ROW_H   = 11;
  const uint8_t Y_START = title ? 13 : 2;
  const uint8_t VISIBLE = (SCREEN_H - Y_START) / ROW_H;

  // Clamp selection
  if (count == 0) return false;
  if (selected >= count) selected = count - 1;

  // Scroll window
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
      display.fillRoundRect(0, y - 1, SCREEN_W - 5, ROW_H, 2, SH110X_WHITE);
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
    uint8_t barY = Y_START + (uint8_t)((long)(SCREEN_H - Y_START - barH) * scrollTop / max(1, count - VISIBLE));
    display.drawFastVLine(SCREEN_W - 2, Y_START, SCREEN_H - Y_START, SH110X_WHITE);
    display.fillRect(SCREEN_W - 3, barY, 3, barH, SH110X_WHITE);
  }

  show();

  // Navigation — caller already called InputManager::update() this frame
  if (InputManager::isPressed(BTN_UP)   && selected > 0)         selected--;
  if (InputManager::isPressed(BTN_DOWN) && selected < count - 1) selected++;
  if (InputManager::isPressed(BTN_SELECT)) return true;
  return false;
}

// ── Message dialog (blocks until any key pressed) ─────────────────
void msgBox(const char* title, const char* msg) {
  clear();
  display.drawRoundRect(2, 2, SCREEN_W - 4, SCREEN_H - 4, 4, SH110X_WHITE);
  display.fillRoundRect(2, 2, SCREEN_W - 4, 13, 4, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  display.setTextSize(1);
  display.setCursor(6, 4);
  display.print(title);
  display.setTextColor(SH110X_WHITE);
  display.drawFastHLine(2, 15, SCREEN_W - 4, SH110X_WHITE);
  text(6, 20, msg);
  display.drawFastHLine(2, 52, SCREEN_W - 4, SH110X_WHITE);
  text(24, 55, "[SELECT] OK");
  show();
  InputManager::waitForPress(0);
}

void progressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t percent) {
  display.drawRect(x, y, w, h, SH110X_WHITE);
  int16_t fill = (int32_t)percent * (w - 2) / 100;
  if (fill > 0) display.fillRect(x + 1, y + 1, fill, h - 2, SH110X_WHITE);
}

// ── Animated slide-in transition (direction: 1=right, -1=left) ────
void slideAnim(int8_t dir) {
  // Quick 4-frame slide — only used on app launch for visual polish
  for (uint8_t step = 0; step < 4; step++) {
    int16_t offset = dir * (int16_t)(32 - step * 8);
    display.clearDisplay();
    display.drawFastVLine(64 + offset, 0, SCREEN_H, SH110X_WHITE);
    display.display();
    delay(18);
  }
}

}  // namespace GUI

// ═══════════════════════════════════════════════════════════════════
//  STORAGE LAYER (SD CARD)
// ═══════════════════════════════════════════════════════════════════
namespace Storage {
static bool _mounted = false;

bool begin()   { _mounted = SD.begin(SD_CS_PIN); return _mounted; }
bool mounted() { return _mounted; }

bool writeFile(const char* path, const char* data) {
  if (!_mounted) return false;
  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  f.print(data); f.close(); return true;
}

size_t readFile(const char* path, char* buf, size_t maxLen) {
  if (!_mounted) return 0;
  File f = SD.open(path);
  if (!f) return 0;
  size_t n = f.readBytes(buf, maxLen - 1);
  buf[n] = '\0'; f.close(); return n;
}

void listDir(const char* path, void (*cb)(const char* name, bool isDir)) {
  if (!_mounted) return;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) return;
  File entry;
  while ((entry = dir.openNextFile())) { cb(entry.name(), entry.isDirectory()); entry.close(); }
  dir.close();
}

bool deleteFile(const char* path) { return _mounted ? SD.remove(path) : false; }
uint64_t totalBytes() { return _mounted ? SD.totalBytes() : 0; }
uint64_t usedBytes()  { return _mounted ? SD.usedBytes()  : 0; }
}  // namespace Storage

// ═══════════════════════════════════════════════════════════════════
//  APP BASE CLASS
// ═══════════════════════════════════════════════════════════════════
class App {
public:
  virtual void setup() = 0;
  virtual void loop()  = 0;
  virtual ~App() {}
};

// ═══════════════════════════════════════════════════════════════════
//  APP MANAGER
// ═══════════════════════════════════════════════════════════════════
namespace AppManager {
#define MAX_APPS 20

struct AppEntry { const char* name; App* (*factory)(); };
static AppEntry registry[MAX_APPS];
static uint8_t  appCount   = 0;
static App*     current    = nullptr;
static int8_t   pendingIdx = -1;

void registerApp(const char* name, App* (*factory)()) {
  if (appCount >= MAX_APPS) return;
  registry[appCount++] = { name, factory };
}
void switchTo(uint8_t idx) { if (idx < appCount) pendingIdx = idx; }

void _applySwitch() {
  if (pendingIdx < 0) return;
  if (current) { delete current; current = nullptr; }
  current = registry[pendingIdx].factory();
  if (current) current->setup();
  pendingIdx = -1;
}
void tick() { _applySwitch(); if (current) current->loop(); }
uint8_t count() { return appCount; }
const char* name(uint8_t i) { return i < appCount ? registry[i].name : ""; }
}  // namespace AppManager

// ═══════════════════════════════════════════════════════════════════
//  [LAUNCHER] — Main home screen with animated selection
// ═══════════════════════════════════════════════════════════════════
// Icon glyphs (CP437): each app gets a single character prefix
static const char APP_ICONS[] = {
  '\x07', // Settings  (bullet)
  '\x1A', // File Mgr  (arrow right)
  '\x13', // Sys Mon   (double excl)
  '\xF0', // Snake     (block)
  '\x0F', // Pong      (sun)
  '\xEB', // TicTacToe (sqrt)
  '\xF7', // WiFi      (approx)
  '\xC4', // BLE       (dash)
  '\x7E', // Signal    (tilde)
  '\xE9', // Graph     (theta)
  '\x0E', // Chemistry (music note used as atom)
  '\x0F', // Light     (sun)
};

class LauncherApp : public App {
  uint8_t _sel       = 0;
  uint8_t _scrollTop = 0;
  uint8_t _animTick  = 0;

public:
  void setup() override { _sel = 0; _scrollTop = 0; _animTick = 0; }

  void loop() override {
    uint8_t count       = AppManager::count();
    uint8_t dispCount   = count - 1;  // skip index 0 (Launcher)

    const uint8_t ROW_H   = 11;
    const uint8_t Y_START = 13;
    const uint8_t VISIBLE = (SCREEN_H - Y_START) / ROW_H;

    InputManager::update();

    if (InputManager::isPressed(BTN_UP)   && _sel > 0)            _sel--;
    if (InputManager::isPressed(BTN_DOWN) && _sel < dispCount - 1) _sel++;
    if (InputManager::isPressed(BTN_SELECT)) {
      GUI::slideAnim(1);
      AppManager::switchTo(_sel + 1);
      return;
    }
    if (InputManager::isPressed(BTN_BACK)) _sel = 0;

    if (_sel < _scrollTop) _scrollTop = _sel;
    if (_sel >= _scrollTop + VISIBLE) _scrollTop = _sel - VISIBLE + 1;

    _animTick++;

    GUI::clear();
    // Header with animated dots
    char hdr[20];
    uint8_t dots = (_animTick / 8) % 4;
    snprintf(hdr, sizeof(hdr), "PigeonOS%.*s", dots, "...");
    GUI::header(hdr);

    for (uint8_t i = 0; i < VISIBLE; i++) {
      uint8_t idx = _scrollTop + i;
      if (idx >= dispCount) break;
      bool sel = (idx == _sel);
      int16_t y = Y_START + i * ROW_H;

      if (sel) {
        // Animated highlight: alternate between full and slightly inset rect
        uint8_t pulse = (_animTick / 4) % 2;
        if (pulse)
          display.fillRoundRect(0, y, SCREEN_W - 5, ROW_H, 3, SH110X_WHITE);
        else
          display.fillRoundRect(1, y + 1, SCREEN_W - 7, ROW_H - 2, 2, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      } else {
        display.setTextColor(SH110X_WHITE);
      }

      display.setTextSize(1);
      // Icon
      char icon[3] = { (char)(idx < sizeof(APP_ICONS) ? APP_ICONS[idx] : ' '), ' ', '\0' };
      display.setCursor(4, y + 2);
      display.print(icon);
      display.print(AppManager::name(idx + 1));
    }
    display.setTextColor(SH110X_WHITE);

    // Scrollbar
    if (dispCount > VISIBLE) {
      uint8_t barH = max(4, (int)((SCREEN_H - Y_START) * VISIBLE / dispCount));
      uint8_t barY = Y_START + (uint8_t)((long)(SCREEN_H - Y_START - barH) * _scrollTop / max(1, dispCount - VISIBLE));
      display.drawFastVLine(SCREEN_W - 2, Y_START, SCREEN_H - Y_START, SH110X_WHITE);
      display.fillRect(SCREEN_W - 3, barY, 3, barH, SH110X_WHITE);
    }

    // Footer hint
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  [SETTINGS]
// ═══════════════════════════════════════════════════════════════════
class SettingsApp : public App {
  uint8_t _sel = 0;
  uint8_t _scrollTop = 0;
  static const char* items[];
  static const uint8_t ITEM_COUNT = 5;

public:
  void setup() override { _sel = 0; _scrollTop = 0; }

  void loop() override {
    InputManager::update();

    if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }

    bool picked = GUI::menu(items, ITEM_COUNT, _sel, "Settings", _scrollTop);
    if (picked) {
      switch (_sel) {
        case 0: GUI::msgBox("Display", "128x64 SH1106\nI2C 0x3C 400KHz"); break;
        case 1: {
          char buf[32];
          snprintf(buf, sizeof(buf), "CPU: %d MHz\nDual-core LX6", getCpuFrequencyMhz());
          GUI::msgBox("CPU", buf);
        } break;
        case 2: {
          char buf[32];
          snprintf(buf, sizeof(buf), "%dKB free\n%dKB total",
                   (int)(ESP.getFreeHeap()/1024), (int)(ESP.getHeapSize()/1024));
          GUI::msgBox("Memory", buf);
        } break;
        case 3: {
          char buf[32];
          snprintf(buf, sizeof(buf), "SDK %s\nIDF %s",
                   ESP.getSdkVersion(), esp_get_idf_version());
          GUI::msgBox("Version", buf);
        } break;
        case 4: AppManager::switchTo(0); return;
      }
    }
  }
};
const char* SettingsApp::items[] = {
  "Display info", "CPU info", "Free RAM", "Version", "< Back"
};

// ═══════════════════════════════════════════════════════════════════
//  [FILE MANAGER]
// ═══════════════════════════════════════════════════════════════════
#define FM_MAX_FILES 16
#define FM_NAME_LEN  20

class FileManagerApp : public App {
  char    _names[FM_MAX_FILES][FM_NAME_LEN];
  bool    _isDir[FM_MAX_FILES];
  uint8_t _count = 0;
  uint8_t _sel   = 0;
  uint8_t _scrollTop = 0;

  static FileManagerApp* _inst;
  static void _enumCb(const char* name, bool isDir) {
    if (!_inst || _inst->_count >= FM_MAX_FILES) return;
    strncpy(_inst->_names[_inst->_count], name, FM_NAME_LEN - 1);
    _inst->_names[_inst->_count][FM_NAME_LEN - 1] = '\0';
    _inst->_isDir[_inst->_count] = isDir;
    _inst->_count++;
  }
  void refresh() { _count = 0; _sel = 0; _scrollTop = 0; _inst = this; Storage::listDir("/", _enumCb); }

public:
  void setup() override {
    if (!Storage::mounted()) { GUI::msgBox("File Mgr", "No SD card!"); AppManager::switchTo(0); return; }
    refresh();
  }

  void loop() override {
    InputManager::update();

    if (_count == 0) {
      GUI::clear(); GUI::header("File Manager");
      GUI::text(2, 20, "SD empty or error");
      GUI::text(2, 54, "BACK:home  MENU:refresh");
      GUI::show();
      if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }
      if (InputManager::isPressed(BTN_MENU)) refresh();
      return;
    }

    if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }
    if (InputManager::isPressed(BTN_MENU)) { refresh(); return; }

    static const char* ptrs[FM_MAX_FILES];
    for (uint8_t i = 0; i < _count; i++) ptrs[i] = _names[i];

    if (GUI::menu(ptrs, _count, _sel, "Files", _scrollTop)) {
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
  }
};
FileManagerApp* FileManagerApp::_inst = nullptr;

// ═══════════════════════════════════════════════════════════════════
//  [SYSTEM MONITOR]
// ═══════════════════════════════════════════════════════════════════
class SysMonitorApp : public App {
  uint32_t _lastUpdate = 0;
  uint32_t _fps = 0, _fpsTimer = 0, _fpsCount = 0;

public:
  void setup() override { _lastUpdate = 0; _fps = 0; _fpsTimer = 0; _fpsCount = 0; }

  void loop() override {
    _fpsCount++;
    uint32_t now = millis();
    if (now - _fpsTimer >= 1000) { _fps = _fpsCount; _fpsCount = 0; _fpsTimer = now; }

    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }

    if (now - _lastUpdate < 250) return;
    _lastUpdate = now;

    uint32_t freeHeap  = ESP.getFreeHeap();
    uint32_t totalHeap = ESP.getHeapSize();
    uint8_t  heapPct   = (uint8_t)(100 - (uint32_t)freeHeap * 100 / totalHeap);
    uint32_t uptime    = now / 1000;

    GUI::clear();
    GUI::header("System Monitor");

    char buf[32];
    snprintf(buf, sizeof(buf), "RAM %dK/%dK", (int)(freeHeap/1024), (int)(totalHeap/1024));
    GUI::text(0, 13, buf);
    GUI::progressBar(0, 23, SCREEN_W, 5, heapPct);

    snprintf(buf, sizeof(buf), "CPU: %d MHz", getCpuFrequencyMhz());
    GUI::text(0, 31, buf);

    snprintf(buf, sizeof(buf), "Up: %02d:%02d:%02d",
             (int)(uptime/3600), (int)((uptime%3600)/60), (int)(uptime%60));
    GUI::text(0, 41, buf);

    snprintf(buf, sizeof(buf), "FPS:%-4d SD:%s", (int)_fps, Storage::mounted() ? "OK" : "--");
    GUI::text(0, 51, buf);

    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  GAME: SNAKE  (3 modes)
//   Mode 0 — Normal    : walls wrap, speed constant
//   Mode 1 — Speed     : walls wrap, speed increases each food
//   Mode 2 — Wall-Death: hitting walls = death
// ═══════════════════════════════════════════════════════════════════
#define SNAKE_COLS 21
#define SNAKE_ROWS  8
#define SNAKE_CELL  6
#define SNAKE_MAX  (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_Y_OFF 13
#define SNAKE_BASE_DELAY 150

class SnakeApp : public App {
  struct Pt { int8_t x, y; };

  Pt      _body[SNAKE_MAX];
  int16_t _len;
  Pt      _dir, _nextDir;
  Pt      _food;
  bool    _dead;
  int16_t _score;
  uint32_t _lastMove;
  uint32_t _moveDelay;

  // Mode selection state
  uint8_t _mode;       // 0=Normal,1=Speed,2=WallDeath
  bool    _picking;    // true while in mode-select screen
  uint8_t _modeSel;
  uint8_t _modeScroll;

  void placeFood() {
    uint8_t attempts = 0;
    do { _food = { (int8_t)random(SNAKE_COLS), (int8_t)random(SNAKE_ROWS) }; attempts++; }
    while (attempts < 100 && onSnake(_food));
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
    else      display.drawRect(px, py, SNAKE_CELL, SNAKE_CELL, SH110X_WHITE);
  }

  void startGame() {
    _len  = 3; _dir = { 1, 0 }; _nextDir = _dir;
    _dead = false; _score = 0; _lastMove = 0;
    _moveDelay = SNAKE_BASE_DELAY;
    for (int16_t i = 0; i < _len; i++)
      _body[i] = { (int8_t)(SNAKE_COLS/2 - i), (int8_t)(SNAKE_ROWS/2) };
    placeFood();
  }

public:
  void setup() override {
    _picking = true; _modeSel = 0; _modeScroll = 0;
  }

  void loop() override {
    InputManager::update();

    // ── Mode selection ──────────────────────────────────────────
    if (_picking) {
      static const char* modeNames[] = { "Normal", "Speed Run", "Wall Death" };
      if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }
      if (GUI::menu(modeNames, 3, _modeSel, "Snake Mode", _modeScroll)) {
        _mode = _modeSel;
        _picking = false;
        startGame();
      }
      return;
    }

    // ── Back to launcher ────────────────────────────────────────
    if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }

    // ── Direction input (queue next direction, no 180° reversal) ─
    if (InputManager::isPressed(BTN_UP)    && _dir.y == 0) _nextDir = { 0, -1 };
    if (InputManager::isPressed(BTN_DOWN)  && _dir.y == 0) _nextDir = { 0,  1 };
    if (InputManager::isPressed(BTN_LEFT)  && _dir.x == 0) _nextDir = {-1,  0 };
    if (InputManager::isPressed(BTN_RIGHT) && _dir.x == 0) _nextDir = { 1,  0 };

    // ── Dead screen ─────────────────────────────────────────────
    if (_dead) {
      GUI::clear();
      GUI::header("Snake - Game Over");
      GUI::text(14, 22, "GAME OVER");
      char buf[20];
      snprintf(buf, sizeof(buf), "Score: %d", _score);
      GUI::text(20, 34, buf);
      const char* modeLabel[] = { "Normal","Speed","WallDeath" };
      GUI::text(4, 44, modeLabel[_mode]);
      GUI::text(0, 55, "SEL:retry  BACK:menu");
      GUI::show();
      if (InputManager::isPressed(BTN_SELECT)) startGame();
      return;
    }

    // ── Move tick ───────────────────────────────────────────────
    uint32_t now = millis();
    if (now - _lastMove >= _moveDelay) {
      _lastMove = now;
      _dir = _nextDir;

      Pt head = { (int8_t)(_body[0].x + _dir.x), (int8_t)(_body[0].y + _dir.y) };

      if (_mode == 2) {
        // Wall death
        if (head.x < 0 || head.x >= SNAKE_COLS || head.y < 0 || head.y >= SNAKE_ROWS) {
          _dead = true; return;
        }
      } else {
        head.x = (head.x + SNAKE_COLS) % SNAKE_COLS;
        head.y = (head.y + SNAKE_ROWS) % SNAKE_ROWS;
      }

      if (onSnake(head)) { _dead = true; return; }

      bool ate = (head.x == _food.x && head.y == _food.y);

      for (int16_t i = min(_len, (int16_t)(SNAKE_MAX - 1)); i > 0; i--)
        _body[i] = _body[i - 1];
      _body[0] = head;

      if (ate) {
        if (_len < SNAKE_MAX) _len++;
        _score++;
        placeFood();
        if (_mode == 1) {
          // Speed: shave 5ms per food, min 60ms
          _moveDelay = max((uint32_t)60, _moveDelay - 5);
        }
      }
    }

    // ── Draw ───────────────────────────────────────────────────
    GUI::clear();

    // Header shows mode initial
    char hdr[16];
    const char* modeAbbr[] = { "NRM", "SPD", "WLD" };
    snprintf(hdr, sizeof(hdr), "Snake[%s] %d", modeAbbr[_mode], _score);
    GUI::header(hdr);

    for (int16_t i = 0; i < _len; i++) {
      // Head looks different
      if (i == 0) {
        int16_t px = _body[0].x * SNAKE_CELL + 1;
        int16_t py = SNAKE_Y_OFF + _body[0].y * SNAKE_CELL + 1;
        display.fillRect(px, py, SNAKE_CELL - 1, SNAKE_CELL - 1, SH110X_WHITE);
        // Eye
        display.drawPixel(px + 1, py + 1, SH110X_BLACK);
      } else {
        drawCell(_body[i].x, _body[i].y);
      }
    }
    drawCell(_food.x, _food.y, false);

    // In WallDeath mode: draw border to warn player
    if (_mode == 2) {
      display.drawRect(0, SNAKE_Y_OFF, SNAKE_COLS * SNAKE_CELL, SNAKE_ROWS * SNAKE_CELL, SH110X_WHITE);
    }

    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  GAME: PONG  (3 modes)
//   Mode 0 — 1P vs AI    : player left, AI right
//   Mode 1 — 2P Local    : UP/DN left, LEFT/RIGHT right
//   Mode 2 — Turbo vs AI : faster ball, harder AI
// ═══════════════════════════════════════════════════════════════════
#define PONG_PADDLE_H  14
#define PONG_PADDLE_W   3
#define PONG_BALL_R     2
#define PONG_Y_MIN     12

class PongApp : public App {
  float   _bx, _by, _bdx, _bdy;
  int16_t _p1y, _p2y;
  uint8_t _score1, _score2;
  uint32_t _lastFrame;
  uint8_t _mode;
  bool    _picking;
  uint8_t _modeSel, _modeScroll;
  uint8_t _winScore;

  void resetBall(int dir) {
    _bx  = SCREEN_W / 2;
    _by  = PONG_Y_MIN + (SCREEN_H - PONG_Y_MIN) / 2;
    float spd = (_mode == 2) ? 3.0f : 2.0f;
    _bdx = spd * dir;
    _bdy = (random(2) ? 1 : -1) * spd * 0.8f;
  }

  void clamp(int16_t& y) {
    if (y < PONG_Y_MIN) y = PONG_Y_MIN;
    if (y + PONG_PADDLE_H > SCREEN_H) y = SCREEN_H - PONG_PADDLE_H;
  }

  void startGame() {
    _p1y = _p2y = PONG_Y_MIN + (SCREEN_H - PONG_Y_MIN - PONG_PADDLE_H) / 2;
    _score1 = _score2 = 0;
    resetBall(1);
    _lastFrame = 0;
    _winScore = 7;
  }

public:
  void setup() override { _picking = true; _modeSel = 0; _modeScroll = 0; }

  void loop() override {
    InputManager::update();

    // ── Mode select ─────────────────────────────────────────────
    if (_picking) {
      static const char* modes[] = { "1P vs AI", "2P Local", "Turbo AI" };
      if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }
      if (GUI::menu(modes, 3, _modeSel, "Pong Mode", _modeScroll)) {
        _mode = _modeSel;
        _picking = false;
        startGame();
      }
      return;
    }

    if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }

    // ── Player 1 (left paddle, UP/DN) ───────────────────────────
    if (InputManager::isDown(BTN_UP))   { _p1y -= 3; clamp(_p1y); }
    if (InputManager::isDown(BTN_DOWN)) { _p1y += 3; clamp(_p1y); }

    // ── Player 2 or AI ──────────────────────────────────────────
    if (_mode == 1) {
      // 2P: LEFT/RIGHT buttons move right paddle
      if (InputManager::isDown(BTN_LEFT))  { _p2y -= 3; clamp(_p2y); }
      if (InputManager::isDown(BTN_RIGHT)) { _p2y += 3; clamp(_p2y); }
    } else {
      // AI: tracks ball with speed limit; harder in Turbo
      int aiSpd = (_mode == 2) ? 3 : 2;
      float mid2 = _p2y + PONG_PADDLE_H / 2.0f;
      if (mid2 < _by - 2) _p2y += aiSpd;
      else if (mid2 > _by + 2) _p2y -= aiSpd;
      clamp(_p2y);
    }

    // ── Physics ─────────────────────────────────────────────────
    uint32_t now = millis();
    if (now - _lastFrame >= 30) {
      _lastFrame = now;
      _bx += _bdx; _by += _bdy;

      // Top/bottom bounce
      if (_by <= PONG_Y_MIN + PONG_BALL_R) { _by = PONG_Y_MIN + PONG_BALL_R; _bdy = -_bdy; }
      if (_by >= SCREEN_H - PONG_BALL_R)   { _by = SCREEN_H - PONG_BALL_R;   _bdy = -_bdy; }

      // Paddle 1
      if (_bx <= PONG_PADDLE_W + PONG_BALL_R + 2 &&
          _by >= _p1y && _by <= _p1y + PONG_PADDLE_H) {
        _bdx = fabsf(_bdx) * 1.05f;
        _bx  = PONG_PADDLE_W + PONG_BALL_R + 3;
        // Add angle based on hit position
        float rel = (_by - _p1y) / (float)PONG_PADDLE_H - 0.5f;
        _bdy = rel * 4.0f;
      }
      // Paddle 2
      if (_bx >= SCREEN_W - PONG_PADDLE_W - PONG_BALL_R - 2 &&
          _by >= _p2y && _by <= _p2y + PONG_PADDLE_H) {
        _bdx = -fabsf(_bdx) * 1.05f;
        _bx  = SCREEN_W - PONG_PADDLE_W - PONG_BALL_R - 3;
        float rel = (_by - _p2y) / (float)PONG_PADDLE_H - 0.5f;
        _bdy = rel * 4.0f;
      }

      // Cap speed
      if (fabsf(_bdx) > 6.0f) _bdx = (_bdx > 0) ? 6.0f : -6.0f;

      // Score
      if (_bx < 0)          { _score2++; resetBall(1);  }
      if (_bx > SCREEN_W)   { _score1++; resetBall(-1); }
    }

    // ── Win check ───────────────────────────────────────────────
    if (_score1 >= _winScore || _score2 >= _winScore) {
      GUI::clear(); GUI::header("Pong - Winner!");
      const char* who = (_mode == 1)
        ? (_score1 >= _winScore ? "Player 1 wins!" : "Player 2 wins!")
        : (_score1 >= _winScore ? "You win!"       : "AI wins!");
      GUI::text(10, 28, who);
      GUI::text(0, 52, "SEL:replay  BACK:menu");
      GUI::show();
      if (InputManager::isPressed(BTN_SELECT)) startGame();
      return;
    }

    // ── Draw ────────────────────────────────────────────────────
    GUI::clear();
    const char* modeLabel[] = { "1P", "2P", "TRB" };
    char hdr[16];
    snprintf(hdr, sizeof(hdr), "Pong[%s] %d|%d", modeLabel[_mode], _score1, _score2);
    GUI::header(hdr);

    display.fillRect(1, _p1y, PONG_PADDLE_W, PONG_PADDLE_H, SH110X_WHITE);
    display.fillRect(SCREEN_W - 1 - PONG_PADDLE_W, _p2y, PONG_PADDLE_W, PONG_PADDLE_H, SH110X_WHITE);
    display.fillCircle((int16_t)_bx, (int16_t)_by, PONG_BALL_R, SH110X_WHITE);

    for (int16_t y = PONG_Y_MIN; y < SCREEN_H; y += 5)
      display.drawPixel(SCREEN_W / 2, y, SH110X_WHITE);

    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  GAME: TIC-TAC-TOE  (3 modes)
//   Mode 0 — vs AI (easy)   : simple heuristic
//   Mode 1 — vs Player 2    : two humans, UP/DN/L/R alternate
//   Mode 2 — vs AI (hard)   : minimax (unbeatable)
// ═══════════════════════════════════════════════════════════════════
class TicTacToeApp : public App {
  int8_t  _board[9];
  uint8_t _cursor;
  int8_t  _turn;    // 1=X, -1=O
  bool    _done;
  uint8_t _xWins, _oWins, _draws;
  uint8_t _mode;
  bool    _picking;
  uint8_t _modeSel, _modeScroll;

  int8_t checkWin() {
    const uint8_t wins[8][3] = {
      {0,1,2},{3,4,5},{6,7,8},
      {0,3,6},{1,4,7},{2,5,8},
      {0,4,8},{2,4,6}
    };
    for (auto& w : wins) {
      int8_t s = _board[w[0]] + _board[w[1]] + _board[w[2]];
      if (s == 3)  return  1;
      if (s == -3) return -1;
    }
    return 0;
  }

  bool boardFull() {
    for (uint8_t i = 0; i < 9; i++) if (!_board[i]) return false;
    return true;
  }

  // Easy AI: win → block → center → random
  uint8_t easyAI() {
    for (uint8_t i = 0; i < 9; i++) {
      if (_board[i]) continue;
      _board[i] = -1; if (checkWin() == -1) { _board[i] = 0; return i; } _board[i] = 0;
    }
    for (uint8_t i = 0; i < 9; i++) {
      if (_board[i]) continue;
      _board[i] =  1; if (checkWin() ==  1) { _board[i] = 0; return i; } _board[i] = 0;
    }
    if (!_board[4]) return 4;
    uint8_t m, tries = 0;
    do { m = random(9); tries++; } while (_board[m] && tries < 30);
    return m;
  }

  // Hard AI: minimax
  int8_t minimax(bool isMax, int8_t depth) {
    int8_t w = checkWin();
    if (w != 0)    return w * (10 - depth);
    if (boardFull()) return 0;
    int8_t best = isMax ? -127 : 127;
    for (uint8_t i = 0; i < 9; i++) {
      if (_board[i]) continue;
      _board[i] = isMax ? -1 : 1;
      int8_t score = minimax(!isMax, depth + 1);
      _board[i] = 0;
      if (isMax) best = max(best, score);
      else       best = min(best, score);
    }
    return best;
  }

  uint8_t hardAI() {
    int8_t best = -127; uint8_t move = 0;
    for (uint8_t i = 0; i < 9; i++) {
      if (_board[i]) continue;
      _board[i] = -1;
      int8_t score = minimax(false, 0);
      _board[i] = 0;
      if (score > best) { best = score; move = i; }
    }
    return move;
  }

  void drawBoard() {
    const int16_t GX = 40, GY = 13, CS = 16;
    GUI::clear();
    GUI::header("Tic-Tac-Toe");

    display.drawFastVLine(GX + CS,     GY, CS * 3, SH110X_WHITE);
    display.drawFastVLine(GX + CS * 2, GY, CS * 3, SH110X_WHITE);
    display.drawFastHLine(GX, GY + CS,     CS * 3, SH110X_WHITE);
    display.drawFastHLine(GX, GY + CS * 2, CS * 3, SH110X_WHITE);

    for (uint8_t i = 0; i < 9; i++) {
      int16_t cx = GX + (i % 3) * CS + CS / 2;
      int16_t cy = GY + (i / 3) * CS + CS / 2 - 3;
      if (_board[i] ==  1) display.drawChar(cx - 3, cy, 'X', SH110X_WHITE, SH110X_BLACK, 1);
      if (_board[i] == -1) display.drawChar(cx - 3, cy, 'O', SH110X_WHITE, SH110X_BLACK, 1);
      if (i == _cursor && !_done) {
        display.drawRect(GX + (i%3)*CS + 1, GY + (i/3)*CS + 1, CS - 2, CS - 2, SH110X_WHITE);
      }
    }

    // Score sidebar (left of board)
    char sb[12];
    snprintf(sb, sizeof(sb), "X:%d", _xWins); GUI::text(0, 13, sb);
    snprintf(sb, sizeof(sb), "O:%d", _oWins); GUI::text(0, 23, sb);
    snprintf(sb, sizeof(sb), "D:%d", _draws); GUI::text(0, 33, sb);

    int8_t w = checkWin();
    if (w || boardFull()) {
      const char* msg = (w == 1) ? "X wins!" : (w == -1 ? "O wins!" : "Draw!");
      GUI::text(0, 55, msg);
      GUI::text(56, 55, "SEL:new");
    } else {
      const char* modeAbbr[] = { "EasyAI", "2P", "HardAI" };
      char tb[20];
      snprintf(tb, sizeof(tb), "%s:%s", modeAbbr[_mode], _turn == 1 ? "X" : "O");
      GUI::text(0, 55, tb);
    }
    GUI::show();
  }

  void newGame() {
    memset(_board, 0, sizeof(_board));
    _cursor = 4; _turn = 1; _done = false;
  }

public:
  void setup() override {
    _picking = true; _modeSel = 0; _modeScroll = 0;
    _xWins = 0; _oWins = 0; _draws = 0;
  }

  void loop() override {
    InputManager::update();

    if (_picking) {
      static const char* modes[] = { "vs AI (Easy)", "vs Player 2", "vs AI (Hard)" };
      if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }
      if (GUI::menu(modes, 3, _modeSel, "TTT Mode", _modeScroll)) {
        _mode = _modeSel; _picking = false; newGame();
      }
      return;
    }

    if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }

    if (_done) {
      drawBoard();
      if (InputManager::isPressed(BTN_SELECT)) newGame();
      return;
    }

    bool isHumanTurn = (_turn == 1) || (_mode == 1);  // mode1 = always human

    if (isHumanTurn) {
      if (InputManager::isPressed(BTN_LEFT)  && _cursor % 3 > 0)  _cursor--;
      if (InputManager::isPressed(BTN_RIGHT) && _cursor % 3 < 2)  _cursor++;
      if (InputManager::isPressed(BTN_UP)    && _cursor / 3 > 0)  _cursor -= 3;
      if (InputManager::isPressed(BTN_DOWN)  && _cursor / 3 < 2)  _cursor += 3;

      if (InputManager::isPressed(BTN_SELECT) && !_board[_cursor]) {
        _board[_cursor] = _turn;
        int8_t w = checkWin();
        if (w || boardFull()) {
          _done = true;
          if (w == 1) _xWins++; else if (w == -1) _oWins++; else _draws++;
        } else {
          _turn = -_turn;
        }
      }
    } else {
      // AI turn
      drawBoard(); delay(300);
      uint8_t m = (_mode == 2) ? hardAI() : easyAI();
      _board[m] = _turn;
      int8_t w = checkWin();
      if (w || boardFull()) {
        _done = true;
        if (w == 1) _xWins++; else if (w == -1) _oWins++; else _draws++;
      } else {
        _turn = -_turn;
      }
    }

    drawBoard();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  NETWORK: WIFI SCANNER
// ═══════════════════════════════════════════════════════════════════
class WiFiScannerApp : public App {
  int     _found   = 0;
  uint8_t _sel     = 0;
  uint8_t _scroll  = 0;
  bool    _scanning = false;

public:
  void setup() override { _found = 0; _sel = 0; _scroll = 0; _scanning = true; WiFi.mode(WIFI_STA); }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) { WiFi.mode(WIFI_OFF); AppManager::switchTo(0); return; }

    if (_scanning) {
      GUI::clear(); GUI::header("WiFi Scanner");
      // Animated scanning dots
      static uint8_t dot = 0;
      char msg[20]; snprintf(msg, sizeof(msg), "Scanning%.*s", (int)(dot % 4), "...");
      GUI::text(10, 28, msg); GUI::show();
      dot++;
      _found    = WiFi.scanNetworks();
      _scanning = false; _sel = 0; _scroll = 0;
      return;
    }

    if (InputManager::isPressed(BTN_MENU)) { _scanning = true; return; }

    if (_found <= 0) {
      GUI::clear(); GUI::header("WiFi Scanner");
      GUI::text(0, 20, "No networks found");
      GUI::text(0, 54, "MENU:scan  BACK:exit");
      GUI::show(); return;
    }

    if (InputManager::isPressed(BTN_UP)   && _sel > 0)                       _sel--;
    if (InputManager::isPressed(BTN_DOWN) && _sel < (uint8_t)(_found - 1))   _sel++;

    const uint8_t VISIBLE = 5;
    if (_sel < _scroll) _scroll = _sel;
    if (_sel >= _scroll + VISIBLE) _scroll = _sel - VISIBLE + 1;

    GUI::clear(); GUI::header("WiFi Networks");

    for (uint8_t i = 0; i < VISIBLE; i++) {
      uint8_t idx = _scroll + i;
      if (idx >= (uint8_t)_found) break;
      bool sel = (idx == _sel);
      int16_t y = 13 + i * 10;
      if (sel) {
        display.fillRect(0, y - 1, SCREEN_W - 20, 10, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      } else {
        display.setTextColor(SH110X_WHITE);
      }
      display.setTextSize(1); display.setCursor(2, y);
      // Truncate SSID
      String ssid = WiFi.SSID(idx);
      if (ssid.length() > 14) ssid = ssid.substring(0, 14);
      display.print(ssid);
      display.setTextColor(SH110X_WHITE);

      // RSSI bars
      int rssi = WiFi.RSSI(idx);
      uint8_t bars = rssi > -55 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1;
      for (uint8_t b = 0; b < 4; b++) {
        if (b < bars) display.fillRect(SCREEN_W - 18 + b * 4, y + 6 - b * 2, 3, b * 2 + 2, SH110X_WHITE);
        else          display.drawRect(SCREEN_W - 18 + b * 4, y + 6 - b * 2, 3, b * 2 + 2, SH110X_WHITE);
      }
    }
    display.setTextColor(SH110X_WHITE);

    if (InputManager::isPressed(BTN_SELECT)) {
      char info[32];
      snprintf(info, sizeof(info), "RSSI:%d Ch:%d", WiFi.RSSI(_sel), WiFi.channel(_sel));
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
  char    _devNames[BLE_MAX_DEVS][BLE_NAME_LEN];
  int     _devRSSI[BLE_MAX_DEVS];
  uint8_t _devCount = 0;
  uint8_t _sel      = 0;
  uint8_t _scroll   = 0;
  bool    _scanning = false;
  BLEScan* _scan    = nullptr;

public:
  void setup() override {
    _devCount = 0; _sel = 0; _scroll = 0;
    BLEDevice::init("");
    _scan = BLEDevice::getScan();
    _scan->setActiveScan(true);
    _scanning = true;
  }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) { BLEDevice::deinit(true); AppManager::switchTo(0); return; }

    if (_scanning) {
      GUI::clear(); GUI::header("BLE Scanner");
      GUI::text(10, 28, "Scanning 3s..."); GUI::show();

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
      _scanning = false; _sel = 0; _scroll = 0;
      return;
    }

    if (InputManager::isPressed(BTN_MENU)) { _scanning = true; return; }

    GUI::clear(); GUI::header("BLE Devices");

    if (_devCount == 0) {
      GUI::text(0, 22, "No devices found");
      GUI::text(0, 54, "MENU:scan  BACK:exit");
    } else {
      if (InputManager::isPressed(BTN_UP)   && _sel > 0)            _sel--;
      if (InputManager::isPressed(BTN_DOWN) && _sel < _devCount - 1) _sel++;

      const uint8_t VISIBLE = 5;
      if (_sel < _scroll) _scroll = _sel;
      if (_sel >= _scroll + VISIBLE) _scroll = _sel - VISIBLE + 1;

      for (uint8_t i = 0; i < VISIBLE; i++) {
        uint8_t idx = _scroll + i;
        if (idx >= _devCount) break;
        bool sel = (idx == _sel);
        int16_t y = 13 + i * 10;
        if (sel) { display.fillRect(0, y - 1, SCREEN_W, 10, SH110X_WHITE); display.setTextColor(SH110X_BLACK); }
        else display.setTextColor(SH110X_WHITE);
        display.setTextSize(1); display.setCursor(2, y);
        display.print(_devNames[idx]);
        char rssi[8]; snprintf(rssi, sizeof(rssi), "%d", _devRSSI[idx]);
        display.setCursor(100, y); display.print(rssi);
      }
      display.setTextColor(SH110X_WHITE);
    }
    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  NETWORK: SIGNAL MONITOR
// ═══════════════════════════════════════════════════════════════════
#define SIG_SAMPLES 100

class SignalMonitorApp : public App {
  int8_t   _rssi[SIG_SAMPLES];
  uint8_t  _head = 0;
  bool     _connected = false;
  uint32_t _lastSample = 0;
  char     _ssid[33] = "";

public:
  void setup() override {
    memset(_rssi, -100, sizeof(_rssi));
    _head = 0; WiFi.mode(WIFI_STA);
    _connected = (WiFi.status() == WL_CONNECTED);
    if (_connected) strncpy(_ssid, WiFi.SSID().c_str(), 32);
  }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) { WiFi.mode(WIFI_OFF); AppManager::switchTo(0); return; }

    uint32_t now = millis();
    if (now - _lastSample >= 500) {
      _lastSample = now;
      _connected  = (WiFi.status() == WL_CONNECTED);
      _rssi[_head] = _connected ? (int8_t)WiFi.RSSI() : -100;
      _head = (_head + 1) % SIG_SAMPLES;
    }

    GUI::clear(); GUI::header("Signal Monitor");

    if (!_connected) {
      GUI::text(0, 22, "Not connected");
      GUI::text(0, 32, "to any WiFi");
    } else {
      GUI::text(0, 12, _ssid);
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
      int8_t cur = _rssi[(_head + SIG_SAMPLES - 1) % SIG_SAMPLES];
      char buf[12]; snprintf(buf, sizeof(buf), "%d dBm", cur);
      GUI::text(80, 12, buf);
    }
    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  UTILITY: GRAPHING CALCULATOR
// ═══════════════════════════════════════════════════════════════════
class GraphCalcApp : public App {
  uint8_t _func = 0;
  float   _xOff = 0.0f;
  float   _zoom = 1.0f;

  float evalFunc(float x) {
    switch (_func) {
      case 0: return sinf(x);
      case 1: return cosf(x);
      case 2: return tanf(x * 0.3f) * 0.5f;
      case 3: return x * x * 0.1f;
      case 4: return sinf(x) * cosf(x * 0.5f);
      case 5: return expf(-x * x * 0.5f);  // Gaussian
    }
    return 0;
  }

  static const char* funcName(uint8_t f) {
    const char* names[] = { "sin(x)", "cos(x)", "tan(0.3x)", "x^2", "sin*cos", "gaussian" };
    return (f < 6) ? names[f] : "";
  }

public:
  void setup() override { _func = 0; _xOff = 0.0f; _zoom = 1.0f; }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK))  { AppManager::switchTo(0); return; }
    if (InputManager::isPressed(BTN_SELECT) || InputManager::isPressed(BTN_RIGHT))
      _func = (_func + 1) % 6;
    if (InputManager::isPressed(BTN_LEFT) && _func > 0) _func--;
    // UP/DOWN pan, HOLD UP/DOWN zoom
    if (InputManager::isDown(BTN_UP))   _xOff -= 0.1f;
    if (InputManager::isDown(BTN_DOWN)) _xOff += 0.1f;

    GUI::clear();
    GUI::header("Graph Calc");

    char lbl[20];
    snprintf(lbl, sizeof(lbl), "y=%s", funcName(_func));
    GUI::text(2, 1, lbl);

    const int16_t PY_MID = 13 + 25, PY_TOP = 13, PY_BOT = 63;
    const float X_RANGE = 6.28f * 2;
    const float Y_SCALE = 22.0f;

    display.drawFastHLine(0, PY_MID, SCREEN_W, SH110X_WHITE);
    display.drawFastVLine(SCREEN_W / 2, PY_TOP, PY_BOT - PY_TOP, SH110X_WHITE);

    int16_t prevPy = -1;
    for (int16_t px = 0; px < SCREEN_W; px++) {
      float x = ((float)px / SCREEN_W - 0.5f) * X_RANGE + _xOff;
      float y = evalFunc(x);
      int16_t py = (int16_t)(PY_MID - y * Y_SCALE);
      if (py < PY_TOP || py > PY_BOT) { prevPy = -1; continue; }
      if (prevPy >= PY_TOP && prevPy <= PY_BOT) {
        display.drawLine(px - 1, prevPy, px, py, SH110X_WHITE);
      } else {
        display.drawPixel(px, py, SH110X_WHITE);
      }
      prevPy = py;
    }

    GUI::text(0, 55, "L/R:func  U/D:pan");
    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  UTILITY: CHEMISTRY APP — ALL 118 ELEMENTS
//  FIX: PROGMEM removed (non-POD). Stored in plain flash via const.
// ═══════════════════════════════════════════════════════════════════
struct ChemElement {
  uint8_t     num;
  const char* symbol;
  const char* name;
  float       mass;
};

// NOTE: PROGMEM is removed — ChemElement has pointer members (non-POD).
// Flash placement is handled by the linker for const data automatically.
static const ChemElement ELEMENTS[] = {
  {  1, "H",  "Hydrogen",     1.008f  },
  {  2, "He", "Helium",       4.003f  },
  {  3, "Li", "Lithium",      6.941f  },
  {  4, "Be", "Beryllium",    9.012f  },
  {  5, "B",  "Boron",       10.811f  },
  {  6, "C",  "Carbon",      12.011f  },
  {  7, "N",  "Nitrogen",    14.007f  },
  {  8, "O",  "Oxygen",      15.999f  },
  {  9, "F",  "Fluorine",    18.998f  },
  { 10, "Ne", "Neon",        20.180f  },
  { 11, "Na", "Sodium",      22.990f  },
  { 12, "Mg", "Magnesium",   24.305f  },
  { 13, "Al", "Aluminum",    26.982f  },
  { 14, "Si", "Silicon",     28.086f  },
  { 15, "P",  "Phosphorus",  30.974f  },
  { 16, "S",  "Sulfur",      32.065f  },
  { 17, "Cl", "Chlorine",    35.453f  },
  { 18, "Ar", "Argon",       39.948f  },
  { 19, "K",  "Potassium",   39.098f  },
  { 20, "Ca", "Calcium",     40.078f  },
  { 21, "Sc", "Scandium",    44.956f  },
  { 22, "Ti", "Titanium",    47.867f  },
  { 23, "V",  "Vanadium",    50.942f  },
  { 24, "Cr", "Chromium",    51.996f  },
  { 25, "Mn", "Manganese",   54.938f  },
  { 26, "Fe", "Iron",        55.845f  },
  { 27, "Co", "Cobalt",      58.933f  },
  { 28, "Ni", "Nickel",      58.693f  },
  { 29, "Cu", "Copper",      63.546f  },
  { 30, "Zn", "Zinc",        65.38f   },
  { 31, "Ga", "Gallium",     69.723f  },
  { 32, "Ge", "Germanium",   72.630f  },
  { 33, "As", "Arsenic",     74.922f  },
  { 34, "Se", "Selenium",    78.971f  },
  { 35, "Br", "Bromine",     79.904f  },
  { 36, "Kr", "Krypton",     83.798f  },
  { 37, "Rb", "Rubidium",    85.468f  },
  { 38, "Sr", "Strontium",   87.62f   },
  { 39, "Y",  "Yttrium",     88.906f  },
  { 40, "Zr", "Zirconium",   91.224f  },
  { 41, "Nb", "Niobium",     92.906f  },
  { 42, "Mo", "Molybdenum",  95.95f   },
  { 43, "Tc", "Technetium",  98.0f    },
  { 44, "Ru", "Ruthenium",  101.07f   },
  { 45, "Rh", "Rhodium",    102.906f  },
  { 46, "Pd", "Palladium",  106.42f   },
  { 47, "Ag", "Silver",     107.868f  },
  { 48, "Cd", "Cadmium",    112.414f  },
  { 49, "In", "Indium",     114.818f  },
  { 50, "Sn", "Tin",        118.710f  },
  { 51, "Sb", "Antimony",   121.760f  },
  { 52, "Te", "Tellurium",  127.60f   },
  { 53, "I",  "Iodine",     126.904f  },
  { 54, "Xe", "Xenon",      131.293f  },
  { 55, "Cs", "Cesium",     132.905f  },
  { 56, "Ba", "Barium",     137.327f  },
  { 57, "La", "Lanthanum",  138.905f  },
  { 58, "Ce", "Cerium",     140.116f  },
  { 59, "Pr", "Praseodymium",140.908f },
  { 60, "Nd", "Neodymium",  144.242f  },
  { 61, "Pm", "Promethium", 145.0f    },
  { 62, "Sm", "Samarium",   150.36f   },
  { 63, "Eu", "Europium",   151.964f  },
  { 64, "Gd", "Gadolinium", 157.25f   },
  { 65, "Tb", "Terbium",    158.925f  },
  { 66, "Dy", "Dysprosium", 162.500f  },
  { 67, "Ho", "Holmium",    164.930f  },
  { 68, "Er", "Erbium",     167.259f  },
  { 69, "Tm", "Thulium",    168.934f  },
  { 70, "Yb", "Ytterbium",  173.045f  },
  { 71, "Lu", "Lutetium",   174.967f  },
  { 72, "Hf", "Hafnium",    178.49f   },
  { 73, "Ta", "Tantalum",   180.948f  },
  { 74, "W",  "Tungsten",   183.84f   },
  { 75, "Re", "Rhenium",    186.207f  },
  { 76, "Os", "Osmium",     190.23f   },
  { 77, "Ir", "Iridium",    192.217f  },
  { 78, "Pt", "Platinum",   195.084f  },
  { 79, "Au", "Gold",       196.967f  },
  { 80, "Hg", "Mercury",    200.592f  },
  { 81, "Tl", "Thallium",   204.38f   },
  { 82, "Pb", "Lead",       207.2f    },
  { 83, "Bi", "Bismuth",    208.980f  },
  { 84, "Po", "Polonium",   209.0f    },
  { 85, "At", "Astatine",   210.0f    },
  { 86, "Rn", "Radon",      222.0f    },
  { 87, "Fr", "Francium",   223.0f    },
  { 88, "Ra", "Radium",     226.0f    },
  { 89, "Ac", "Actinium",   227.0f    },
  { 90, "Th", "Thorium",    232.038f  },
  { 91, "Pa", "Protactinium",231.036f },
  { 92, "U",  "Uranium",    238.029f  },
  { 93, "Np", "Neptunium",  237.0f    },
  { 94, "Pu", "Plutonium",  244.0f    },
  { 95, "Am", "Americium",  243.0f    },
  { 96, "Cm", "Curium",     247.0f    },
  { 97, "Bk", "Berkelium",  247.0f    },
  { 98, "Cf", "Californium",251.0f    },
  { 99, "Es", "Einsteinium",252.0f    },
  {100, "Fm", "Fermium",    257.0f    },
  {101, "Md", "Mendelevium",258.0f    },
  {102, "No", "Nobelium",   259.0f    },
  {103, "Lr", "Lawrencium", 266.0f    },
  {104, "Rf", "Rutherfordium",267.0f  },
  {105, "Db", "Dubnium",    268.0f    },
  {106, "Sg", "Seaborgium", 269.0f    },
  {107, "Bh", "Bohrium",    270.0f    },
  {108, "Hs", "Hassium",    277.0f    },
  {109, "Mt", "Meitnerium", 278.0f    },
  {110, "Ds", "Darmstadtium",281.0f   },
  {111, "Rg", "Roentgenium",282.0f    },
  {112, "Cn", "Copernicium",285.0f    },
  {113, "Nh", "Nihonium",   286.0f    },
  {114, "Fl", "Flerovium",  289.0f    },
  {115, "Mc", "Moscovium",  290.0f    },
  {116, "Lv", "Livermorium",293.0f    },
  {117, "Ts", "Tennessine", 294.0f    },
  {118, "Og", "Oganesson",  294.0f    },
};
#define CHEM_COUNT 118

class ChemApp : public App {
  uint8_t _sel = 0;

public:
  void setup() override { _sel = 0; }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) { AppManager::switchTo(0); return; }

    if (InputManager::isPressed(BTN_LEFT)  && _sel > 0)                _sel--;
    if (InputManager::isPressed(BTN_RIGHT) && _sel < CHEM_COUNT - 1)   _sel++;
    if (InputManager::isPressed(BTN_UP)    && _sel >= 10)              _sel -= 10;
    if (InputManager::isPressed(BTN_DOWN)  && _sel + 10 < CHEM_COUNT)  _sel += 10;

    // Jump to first/last with held UP/DOWN
    if (InputManager::isHeld(BTN_LEFT))  _sel = 0;
    if (InputManager::isHeld(BTN_RIGHT)) _sel = CHEM_COUNT - 1;

    const ChemElement& e = ELEMENTS[_sel];

    GUI::clear();
    GUI::header("Chemistry");

    // Element box
    display.drawRoundRect(2, 13, 46, 42, 3, SH110X_WHITE);
    char numBuf[4]; itoa(e.num, numBuf, 10);
    GUI::text(5, 15, numBuf);

    // Big symbol (size 2)
    display.setTextSize(2);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(8, 26);
    display.print(e.symbol);
    display.setTextSize(1);

    // Mass beneath symbol
    char massBuf[10]; snprintf(massBuf, sizeof(massBuf), "%.2f", e.mass);
    GUI::text(5, 46, massBuf);

    // Info panel right
    GUI::text(52, 15, e.name);
    char mbuf[16]; snprintf(mbuf, sizeof(mbuf), "%.3f u", e.mass);
    GUI::text(52, 27, "Mass:");
    GUI::text(52, 37, mbuf);

    // Category (crude guess by atomic number)
    const char* cat = "Trans.Metal";
    if      (e.num == 1 || e.num == 2)  cat = "NonMetal";
    else if (e.num <= 4)                cat = "Alkali/AlkE";
    else if (e.num <= 10)               cat = "NonMetal";
    else if (e.num <= 18)               cat = "Various";
    else if (e.num <= 30)               cat = "Trans.Metal";
    else if (e.num >= 57 && e.num <= 71) cat = "Lanthanide";
    else if (e.num >= 89 && e.num <= 103) cat = "Actinide";
    else if (e.num > 103)               cat = "Synthetic";
    GUI::text(52, 47, cat);

    // Navigation hint
    char nav[22];
    snprintf(nav, sizeof(nav), "%d/%d L/R:1  U/D:10", (int)(_sel + 1), CHEM_COUNT);
    GUI::text(0, 56, nav);

    GUI::show();
  }
};

// ═══════════════════════════════════════════════════════════════════
//  UTILITY: LIGHT TOOL (Morse / Torch)
// ═══════════════════════════════════════════════════════════════════
class LightToolApp : public App {
  uint8_t  _mode      = 0;   // 0=off, 1=on, 2=blink, 3=SOS
  uint32_t _lastBlink = 0;
  bool     _blinkState = false;
  uint8_t  _sosStep   = 0;
  uint32_t _sosNext   = 0;

  static const uint16_t SOS_ON[];
  static const uint16_t SOS_OFF[];
  static const uint8_t  SOS_LEN = 9;

public:
  void setup() override { pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW); _mode = 0; _sosStep = 0; }

  void loop() override {
    InputManager::update();
    if (InputManager::isPressed(BTN_BACK)) { digitalWrite(LED_PIN, LOW); AppManager::switchTo(0); return; }

    if (InputManager::isPressed(BTN_UP))                    _mode = (_mode + 1) % 4;
    if (InputManager::isPressed(BTN_DOWN) && _mode > 0)     _mode--;
    if (InputManager::isPressed(BTN_SELECT))                 _mode = (_mode + 1) % 4;

    uint32_t now = millis();
    switch (_mode) {
      case 0: digitalWrite(LED_PIN, LOW);  break;
      case 1: digitalWrite(LED_PIN, HIGH); break;
      case 2:
        if (now - _lastBlink >= 500) { _blinkState = !_blinkState; _lastBlink = now; digitalWrite(LED_PIN, _blinkState); }
        break;
      case 3:
        if (now >= _sosNext) {
          _blinkState = !_blinkState;
          digitalWrite(LED_PIN, _blinkState);
          _sosNext = now + (_blinkState ? SOS_ON[_sosStep] : SOS_OFF[_sosStep]);
          if (!_blinkState) _sosStep = (_sosStep + 1) % SOS_LEN;
        }
        break;
    }

    GUI::clear(); GUI::header("Light Tool");
    const char* modes[] = { "OFF", "ON (torch)", "Blink", "SOS" };
    GUI::text(20, 20, "Mode:");
    GUI::text(20, 32, modes[_mode], 1, _mode > 0);

    // Visual indicator
    if (_mode > 0) {
      uint8_t r = (_blinkState || _mode == 1) ? 8 : 4;
      display.fillCircle(100, 38, r, SH110X_WHITE);
    }
    GUI::text(0, 55, "UP/SEL:mode  BACK:exit");
    GUI::show();
  }
};
const uint16_t LightToolApp::SOS_ON[]  = { 200, 200, 200, 600, 600, 600, 200, 200, 200 };
const uint16_t LightToolApp::SOS_OFF[] = { 200, 200, 600, 200, 200, 200, 600, 200, 800 };

// ═══════════════════════════════════════════════════════════════════
//  APP FACTORY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════
App* makeLauncher()    { return new LauncherApp();      }
App* makeSettings()    { return new SettingsApp();      }
App* makeFileManager() { return new FileManagerApp();   }
App* makeSysMonitor()  { return new SysMonitorApp();    }
App* makeSnake()       { return new SnakeApp();         }
App* makePong()        { return new PongApp();          }
App* makeTicTacToe()   { return new TicTacToeApp();     }
App* makeWiFiScanner() { return new WiFiScannerApp();   }
App* makeBLEScanner()  { return new BLEScannerApp();    }
App* makeSignalMon()   { return new SignalMonitorApp(); }
App* makeGraphCalc()   { return new GraphCalcApp();     }
App* makeChem()        { return new ChemApp();          }
App* makeLightTool()   { return new LightToolApp();     }

// ═══════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("PigeonOS v2.0 booting...");

  Wire.begin(21, 22);
  Wire.setClock(400000);

  bool dispOk = display.begin(OLED_ADDR, true);
  if (!dispOk) dispOk = display.begin(0x3D, true);
  if (!dispOk) {
    Serial.println("SH1106 not found! Check wiring & address.");
    pinMode(LED_PIN, OUTPUT);
    while (true) { digitalWrite(LED_PIN, !digitalRead(LED_PIN)); delay(200); }
  }

  display.cp437(true);
  display.setTextWrap(false);
  display.setTextColor(SH110X_WHITE);

  // ── Animated boot splash ──────────────────────────────────────
  display.clearDisplay();
  display.drawRoundRect(0, 0, SCREEN_W, SCREEN_H, 4, SH110X_WHITE);

  // Animated reveal: draw header fill line by line
  for (int16_t i = 0; i <= 18; i++) {
    display.fillRect(0, 0, SCREEN_W, i, SH110X_WHITE);
    display.display();
    delay(8);
  }

  // Title
  display.setTextColor(SH110X_BLACK);
  display.setTextSize(2);
  display.setCursor(8, 2);
  display.print("PigeonOS");
  display.display(); delay(100);

  // Pixel-art pigeon (12x10, centered)
  // Simple silhouette drawn with pixels
  int16_t px = 58, py = 22;
  // body
  display.fillRoundRect(px, py + 3, 12, 7, 2, SH110X_WHITE);
  // head
  display.fillCircle(px + 10, py + 2, 4, SH110X_WHITE);
  // beak
  display.drawLine(px + 13, py + 2, px + 16, py + 3, SH110X_WHITE);
  // tail
  display.drawLine(px, py + 5, px - 3, py + 3, SH110X_WHITE);
  display.drawLine(px, py + 6, px - 3, py + 8, SH110X_WHITE);
  // eye
  display.drawPixel(px + 11, py + 1, SH110X_BLACK);
  // wing
  display.drawLine(px + 3, py + 4, px + 7, py + 2, SH110X_WHITE);
  display.display(); delay(200);

  // Version line
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(36, 35);
  display.print("v2.0  ESP32");
  display.display(); delay(150);

  // Animated loading bar
  display.drawRect(10, 46, 108, 8, SH110X_WHITE);
  display.display();
  for (uint8_t i = 0; i <= 104; i += 4) {
    display.fillRect(11, 47, i, 6, SH110X_WHITE);
    display.display();
    delay(15);
  }
  display.setCursor(22, 56);
  display.print("Initializing...");
  display.display();
  delay(300);

  // ── SD Card ───────────────────────────────────────────────────
  if (Storage::begin()) Serial.println("SD card mounted OK");
  else Serial.println("SD card not found (optional)");

  // ── Input ─────────────────────────────────────────────────────
  InputManager::begin();

  // ── Register apps ─────────────────────────────────────────────
  AppManager::registerApp("Launcher",    makeLauncher);
  AppManager::registerApp("Settings",    makeSettings);
  AppManager::registerApp("File Manager",makeFileManager);
  AppManager::registerApp("Sys Monitor", makeSysMonitor);
  AppManager::registerApp("Snake",       makeSnake);
  AppManager::registerApp("Pong",        makePong);
  AppManager::registerApp("Tic Tac Toe", makeTicTacToe);
  AppManager::registerApp("WiFi Scanner",makeWiFiScanner);
  AppManager::registerApp("BLE Scanner", makeBLEScanner);
  AppManager::registerApp("Signal Mon.", makeSignalMon);
  AppManager::registerApp("Graph Calc",  makeGraphCalc);
  AppManager::registerApp("Chemistry",   makeChem);
  AppManager::registerApp("Light Tool",  makeLightTool);

  AppManager::switchTo(0);

  Serial.printf("PigeonOS v2.0 ready. %d apps registered.\n", AppManager::count());
  Serial.printf("Free heap: %d bytes\n", (int)ESP.getFreeHeap());
}

// ═══════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════════════════════
void loop() {
  // NOTE: InputManager::update() is intentionally NOT called here.
  // Every app's loop() is responsible for calling it once per frame.
  // This prevents the double-update bug that caused frozen sub-menus.

  // Global MENU button check — done inside each app to avoid race.
  // Apps that want global MENU-to-home handle it themselves.

  AppManager::tick();
  yield();
}