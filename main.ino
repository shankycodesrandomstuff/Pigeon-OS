#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <U8g2lib.h>
#include <string.h>
#include <stdio.h>

// Hardware map
#define PIN_BTN_UP      34
#define PIN_BTN_DOWN    35
#define PIN_BTN_LEFT    32
#define PIN_BTN_RIGHT   33
#define PIN_BTN_SELECT  25
#define PIN_BTN_BACK    26
#define PIN_BTN_MENU    27

#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

#define SD_CS           5
#define SD_MOSI         23
#define SD_MISO         19
#define SD_SCK          18

#define BUILTIN_LED     2
#define KERNEL_TICK_MS  20
#define INPUT_DEBOUNCE_MS       ((uint32_t)20)
#define INPUT_HOLD_INITIAL_MS   ((uint32_t)300)
#define INPUT_HOLD_FAST_MS      ((uint32_t)800)
#define INPUT_REPEAT_NORMAL_MS  ((uint32_t)150)
#define INPUT_REPEAT_FAST_MS    ((uint32_t)70)
#define INPUT_QUEUE_SIZE        12
#define MAX_APPS                20

enum InputEvent : uint8_t {
  EVENT_NONE = 0,
  EVENT_UP,
  EVENT_DOWN,
  EVENT_LEFT,
  EVENT_RIGHT,
  EVENT_SELECT,
  EVENT_BACK,
  EVENT_MENU
};

class InputManager {
public:
  void begin() {
    _qHead = _qTail = _qCount = 0;
    _now = 0;
    for (uint8_t i = 0; i < 7; i++) {
      _buttons[i].pin = PIN_MAP[i];
      _buttons[i].lastRaw = false;
      _buttons[i].stable = false;
      _buttons[i].lastChangeMs = 0;
      _buttons[i].pressStartMs = 0;
      _buttons[i].lastRepeatMs = 0;
      pinMode(PIN_MAP[i], INPUT_PULLUP);
    }
  }

  void poll() {
    _now = millis();
    for (uint8_t i = 0; i < 7; i++) _processButton(_buttons[i], EVENT_MAP[i]);
  }

  bool hasEvent() const { return _qCount > 0; }

  InputEvent peekEvent() const {
    if (_qCount == 0) return EVENT_NONE;
    return _queue[_qHead];
  }

  InputEvent getEvent() {
    if (_qCount == 0) return EVENT_NONE;
    InputEvent e = _queue[_qHead];
    _qHead = (uint8_t)((_qHead + 1) % INPUT_QUEUE_SIZE);
    _qCount--;
    return e;
  }

private:
  struct ButtonState {
    uint8_t pin;
    bool lastRaw;
    bool stable;
    uint32_t lastChangeMs;
    uint32_t pressStartMs;
    uint32_t lastRepeatMs;
  };

  static const uint8_t PIN_MAP[7];
  static const InputEvent EVENT_MAP[7];

  ButtonState _buttons[7];
  InputEvent _queue[INPUT_QUEUE_SIZE];
  uint8_t _qHead, _qTail, _qCount;
  uint32_t _now;

  void _enqueue(InputEvent e) {
    if (_qCount >= INPUT_QUEUE_SIZE) {
      _qHead = (uint8_t)((_qHead + 1) % INPUT_QUEUE_SIZE);
      _qCount--;
    }
    _queue[_qTail] = e;
    _qTail = (uint8_t)((_qTail + 1) % INPUT_QUEUE_SIZE);
    _qCount++;
  }

  void _processButton(ButtonState& b, InputEvent e) {
    bool raw = (digitalRead(b.pin) == LOW);
    if (raw != b.lastRaw) {
      b.lastChangeMs = _now;
      b.lastRaw = raw;
    }
    if ((_now - b.lastChangeMs) < INPUT_DEBOUNCE_MS) return;

    if (raw && !b.stable) {
      b.stable = true;
      b.pressStartMs = _now;
      b.lastRepeatMs = _now;
      _enqueue(e);
    } else if (!raw && b.stable) {
      b.stable = false;
    } else if (raw && b.stable) {
      uint32_t held = _now - b.pressStartMs;
      if (held < INPUT_HOLD_INITIAL_MS) return;
      uint32_t interval = (held >= INPUT_HOLD_FAST_MS) ? INPUT_REPEAT_FAST_MS : INPUT_REPEAT_NORMAL_MS;
      if ((_now - b.lastRepeatMs) >= interval) {
        b.lastRepeatMs = _now;
        _enqueue(e);
      }
    }
  }
};

const uint8_t InputManager::PIN_MAP[7] = {
  PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT,
  PIN_BTN_SELECT, PIN_BTN_BACK, PIN_BTN_MENU
};

const InputEvent InputManager::EVENT_MAP[7] = {
  EVENT_UP, EVENT_DOWN, EVENT_LEFT, EVENT_RIGHT,
  EVENT_SELECT, EVENT_BACK, EVENT_MENU
};

class UIEngine {
public:
  bool begin() {
    Wire.begin(OLED_SDA, OLED_SCL);
    _u8 = &s_display;
    _dirty = false;
    _u8->begin();
    _u8->setFont(u8g2_font_6x10_tf);
    _u8->setFontPosTop();
    _u8->setDrawColor(1);
    _u8->clearBuffer();
    return true;
  }

  void clear() { _u8->clearBuffer(); _dirty = true; }
  void render() { if (_dirty) { _u8->sendBuffer(); _dirty = false; } }

  void drawText(uint8_t x, uint8_t y, const char* t) { if (t) { _u8->drawStr(x, y, t); _dirty = true; } }
  void drawFrame(uint8_t x, uint8_t y, uint8_t w, uint8_t h) { _u8->drawFrame(x, y, w, h); _dirty = true; }
  void drawBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h) { _u8->drawBox(x, y, w, h); _dirty = true; }

  void drawHeader(const char* title) {
    drawBox(0, 0, OLED_WIDTH, 11);
    _u8->setDrawColor(0);
    _u8->drawStr(2, 1, title ? title : "");
    _u8->setDrawColor(1);
    _dirty = true;
  }

  void drawFooter(const char* left, const char* right) {
    uint8_t y = OLED_HEIGHT - 10;
    _u8->drawHLine(0, y, OLED_WIDTH);
    if (left) _u8->drawStr(1, y + 1, left);
    if (right) {
      uint16_t w = _u8->getStrWidth(right);
      if (w < OLED_WIDTH) _u8->drawStr((uint8_t)(OLED_WIDTH - w - 1), y + 1, right);
    }
    _dirty = true;
  }

  void drawMenu(const char* const* items, uint8_t count, uint8_t selected) {
    const uint8_t rowH = 10;
    const uint8_t rows = 4;
    uint8_t top = (selected >= rows) ? (uint8_t)(selected - rows + 1) : 0;
    for (uint8_t i = 0; i < rows; i++) {
      uint8_t idx = (uint8_t)(top + i);
      if (idx >= count) break;
      uint8_t y = (uint8_t)(12 + i * rowH);
      if (idx == selected) {
        drawBox(0, y, OLED_WIDTH, rowH);
        _u8->setDrawColor(0);
        _u8->drawStr(2, (uint8_t)(y + 1), items[idx]);
        _u8->setDrawColor(1);
      } else {
        _u8->drawStr(2, (uint8_t)(y + 1), items[idx]);
      }
    }
    _dirty = true;
  }

private:
  static U8G2_SH1106_128X64_NONAME_F_HW_I2C s_display;
  U8G2_SH1106_128X64_NONAME_F_HW_I2C* _u8;
  bool _dirty;
};

U8G2_SH1106_128X64_NONAME_F_HW_I2C UIEngine::s_display(U8G2_R0, U8X8_PIN_NONE);

class SDManager {
public:
  SDManager() : _ready(false) {}
  bool begin() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    _ready = SD.begin(SD_CS);
    return _ready;
  }
  bool isReady() const { return _ready; }
private:
  bool _ready;
};

class App {
public:
  virtual ~App() {}
  virtual void setup() = 0;
  virtual void loop() = 0;
};

static const char* eventText(InputEvent e) {
  switch (e) {
    case EVENT_UP: return "UP";
    case EVENT_DOWN: return "DOWN";
    case EVENT_LEFT: return "LEFT";
    case EVENT_RIGHT: return "RIGHT";
    case EVENT_SELECT: return "SELECT";
    case EVENT_BACK: return "BACK";
    case EVENT_MENU: return "MENU";
    default: return "NONE";
  }
}

class SimpleInfoApp : public App {
public:
  SimpleInfoApp(InputManager& in, UIEngine& ui, const char* title, const char* l1, const char* l2)
    : _in(in), _ui(ui), _title(title), _l1(l1), _l2(l2) { strcpy(_last, "NONE"); }

  void setup() override { strcpy(_last, "NONE"); }

  void loop() override {
    while (_in.hasEvent()) {
      strncpy(_last, eventText(_in.getEvent()), sizeof(_last) - 1);
      _last[sizeof(_last) - 1] = '\0';
    }
    _ui.clear();
    _ui.drawHeader(_title);
    _ui.drawFrame(1, 13, 126, 39);
    _ui.drawText(4, 16, _l1);
    _ui.drawText(4, 28, _l2);
    _ui.drawText(4, 40, "Last key:");
    _ui.drawText(48, 40, _last);
    _ui.drawFooter("BACK exit", "MENU alt");
  }

private:
  InputManager& _in;
  UIEngine& _ui;
  const char* _title;
  const char* _l1;
  const char* _l2;
  char _last[10];
};

class SettingsApp : public App {
public:
  SettingsApp(InputManager& in, UIEngine& ui) : _in(in), _ui(ui), _brightness(50), _tick(20) {}
  void setup() override {}
  void loop() override {
    while (_in.hasEvent()) {
      InputEvent e = _in.getEvent();
      if (e == EVENT_UP && _brightness < 100) _brightness += 5;
      else if (e == EVENT_DOWN && _brightness > 5) _brightness -= 5;
      else if (e == EVENT_LEFT && _tick > 10) _tick -= 5;
      else if (e == EVENT_RIGHT && _tick < 50) _tick += 5;
    }

    char b[18], t[18];
    snprintf(b, sizeof(b), "Brightness:%u", _brightness);
    snprintf(t, sizeof(t), "Tick(ms):%u", _tick);
    _ui.clear();
    _ui.drawHeader("Settings");
    _ui.drawFrame(1, 13, 126, 39);
    _ui.drawText(4, 16, b);
    _ui.drawText(4, 28, t);
    _ui.drawText(4, 40, "U/D bright L/R tick");
    _ui.drawFooter("BACK exit", "MENU save");
  }
private:
  InputManager& _in;
  UIEngine& _ui;
  uint8_t _brightness;
  uint8_t _tick;
};

class SystemMonitorApp : public App {
public:
  SystemMonitorApp(InputManager& in, UIEngine& ui, SDManager& sd) : _in(in), _ui(ui), _sd(sd) {}
  void setup() override {}
  void loop() override {
    while (_in.hasEvent()) _in.getEvent();
    uint32_t uptime = millis() / 1000;
    char l1[24], l2[24], l3[24];
    snprintf(l1, sizeof(l1), "Uptime: %lus", (unsigned long)uptime);
    snprintf(l2, sizeof(l2), "Heap: %u", (unsigned)ESP.getFreeHeap());
    snprintf(l3, sizeof(l3), "SD: %s", _sd.isReady() ? "ready" : "not ready");
    _ui.clear();
    _ui.drawHeader("System Monitor");
    _ui.drawFrame(1, 13, 126, 39);
    _ui.drawText(4, 16, l1);
    _ui.drawText(4, 28, l2);
    _ui.drawText(4, 40, l3);
    _ui.drawFooter("BACK exit", "MENU refresh");
  }
private:
  InputManager& _in;
  UIEngine& _ui;
  SDManager& _sd;
};

class LightToolApp : public App {
public:
  LightToolApp(InputManager& in, UIEngine& ui) : _in(in), _ui(ui), _level(0), _strobe(false) {}
  void setup() override { _level = 0; _strobe = false; }
  void loop() override {
    while (_in.hasEvent()) {
      InputEvent e = _in.getEvent();
      if (e == EVENT_UP && _level < 10) _level++;
      else if (e == EVENT_DOWN && _level > 0) _level--;
      else if (e == EVENT_MENU) _strobe = !_strobe;
    }

    bool led = _strobe ? ((millis() / 120) % 2) : (_level > 0);
    digitalWrite(BUILTIN_LED, led ? HIGH : LOW);

    _ui.clear();
    _ui.drawHeader("Light Tool");
    _ui.drawFrame(1, 13, 126, 39);
    _ui.drawText(4, 16, _strobe ? "Mode: STROBE" : "Mode: STEADY");
    _ui.drawText(4, 28, "Intensity:");
    _ui.drawFrame(56, 28, 60, 8);
    _ui.drawBox(58, 30, (uint8_t)(_level * 5), 4);
    _ui.drawFooter("UP/DN level", "MENU strobe");
  }
private:
  InputManager& _in;
  UIEngine& _ui;
  uint8_t _level;
  bool _strobe;
};

class SnakeApp : public App {
public:
  SnakeApp(InputManager& in, UIEngine& ui) : _in(in), _ui(ui) {}
  void setup() override { _x = 8; _y = 4; _dx = 1; _dy = 0; _fx = 3; _fy = 3; _score = 0; _lastStep = 0; }
  void loop() override {
    while (_in.hasEvent()) {
      switch (_in.getEvent()) {
        case EVENT_UP: _dx = 0; _dy = -1; break;
        case EVENT_DOWN: _dx = 0; _dy = 1; break;
        case EVENT_LEFT: _dx = -1; _dy = 0; break;
        case EVENT_RIGHT: _dx = 1; _dy = 0; break;
        case EVENT_SELECT: setup(); return;
        default: break;
      }
    }

    if (millis() - _lastStep > 160) {
      _lastStep = millis();
      _x = (uint8_t)((_x + _dx + 16) % 16);
      _y = (uint8_t)((_y + _dy + 8) % 8);
      if (_x == _fx && _y == _fy) {
        _score++;
        _fx = (uint8_t)((_fx + 5) % 16);
        _fy = (uint8_t)((_fy + 3) % 8);
      }
    }

    char s[16]; snprintf(s, sizeof(s), "Score %u", _score);
    _ui.clear();
    _ui.drawHeader("Snake");
    _ui.drawFrame(1, 13, 96, 39);
    _ui.drawBox((uint8_t)(3 + _x * 5), (uint8_t)(15 + _y * 4), 4, 3);
    _ui.drawFrame((uint8_t)(3 + _fx * 5), (uint8_t)(15 + _fy * 4), 4, 3);
    _ui.drawText(100, 16, s);
    _ui.drawFooter("Arrows move", "SEL reset");
  }
private:
  InputManager& _in;
  UIEngine& _ui;
  uint8_t _x, _y, _fx, _fy, _score;
  int8_t _dx, _dy;
  uint32_t _lastStep;
};

class PongApp : public App {
public:
  PongApp(InputManager& in, UIEngine& ui) : _in(in), _ui(ui) {}
  void setup() override { _px = 40; _bx = 20; _by = 20; _vx = 1; _vy = 1; _score = 0; _lastStep = 0; }
  void loop() override {
    while (_in.hasEvent()) {
      InputEvent e = _in.getEvent();
      if (e == EVENT_LEFT && _px > 2) _px -= 3;
      else if (e == EVENT_RIGHT && _px < 80) _px += 3;
      else if (e == EVENT_SELECT) setup();
    }

    if (millis() - _lastStep > 70) {
      _lastStep = millis();
      _bx += _vx; _by += _vy;
      if (_bx <= 2 || _bx >= 93) _vx = -_vx;
      if (_by <= 14) _vy = -_vy;
      if (_by >= 48) {
        if (_bx >= _px && _bx <= _px + 16) { _vy = -1; _score++; }
        else setup();
      }
    }

    char s[16]; snprintf(s, sizeof(s), "Score %u", _score);
    _ui.clear();
    _ui.drawHeader("Pong");
    _ui.drawFrame(1, 13, 96, 39);
    _ui.drawBox((uint8_t)_px, 48, 16, 2);
    _ui.drawBox((uint8_t)_bx, (uint8_t)_by, 2, 2);
    _ui.drawText(100, 16, s);
    _ui.drawFooter("L/R move", "SEL reset");
  }
private:
  InputManager& _in;
  UIEngine& _ui;
  int16_t _px, _bx, _by;
  int8_t _vx, _vy;
  uint8_t _score;
  uint32_t _lastStep;
};

class TicTacToeApp : public App {
public:
  TicTacToeApp(InputManager& in, UIEngine& ui) : _in(in), _ui(ui) {}
  void setup() override { memset(_c, 0, sizeof(_c)); _cur = 0; _p = 1; _w = 0; }
  void loop() override {
    while (_in.hasEvent()) {
      InputEvent e = _in.getEvent();
      if (e == EVENT_UP && _cur >= 3) _cur -= 3;
      else if (e == EVENT_DOWN && _cur <= 5) _cur += 3;
      else if (e == EVENT_LEFT && (_cur % 3) > 0) _cur--;
      else if (e == EVENT_RIGHT && (_cur % 3) < 2) _cur++;
      else if (e == EVENT_MENU) setup();
      else if (e == EVENT_SELECT) {
        if (_w) { setup(); break; }
        if (_c[_cur] == 0) { _c[_cur] = _p; _p = (uint8_t)(3 - _p); _w = check(); }
      }
    }

    _ui.clear();
    _ui.drawHeader("Tic Tac Toe");
    for (uint8_t r = 0; r < 3; r++) {
      for (uint8_t c = 0; c < 3; c++) {
        uint8_t i = (uint8_t)(r * 3 + c);
        uint8_t x = (uint8_t)(4 + c * 20);
        uint8_t y = (uint8_t)(14 + r * 13);
        _ui.drawFrame(x, y, 18, 11);
        if (i == _cur) _ui.drawFrame((uint8_t)(x - 1), (uint8_t)(y - 1), 20, 13);
        if (_c[i] == 1) _ui.drawText((uint8_t)(x + 6), (uint8_t)(y + 1), "X");
        else if (_c[i] == 2) _ui.drawText((uint8_t)(x + 6), (uint8_t)(y + 1), "O");
      }
    }
    _ui.drawText(72, 18, _w == 1 ? "X wins" : (_w == 2 ? "O wins" : (_p == 1 ? "Turn X" : "Turn O")));
    _ui.drawFooter("Arrows+SEL", "MENU reset");
  }
private:
  uint8_t check() {
    static const uint8_t l[8][3]={{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    for(uint8_t i=0;i<8;i++){uint8_t a=l[i][0],b=l[i][1],c=l[i][2];if(_c[a]&&_c[a]==_c[b]&&_c[a]==_c[c])return _c[a];}
    return 0;
  }
  InputManager& _in; UIEngine& _ui;
  uint8_t _c[9], _cur, _p, _w;
};

enum AppState : uint8_t { APPSTATE_LAUNCHER = 0, APPSTATE_RUNNING };

class AppLoader {
public:
  AppLoader(InputManager& in, UIEngine& ui, SDManager& sd)
    : _in(in), _ui(ui), _sd(sd), _state(APPSTATE_LAUNCHER), _count(0), _sel(0), _active(-1), _showGroupOnly(false) {}

  void begin() { initRegistry(); }

  void update() {
    if (_state == APPSTATE_LAUNCHER) {
      while (_in.hasEvent()) handleLauncher(_in.getEvent());
      drawLauncher();
      return;
    }

    if (_in.peekEvent() == EVENT_BACK) {
      _in.getEvent();
      _state = APPSTATE_LAUNCHER;
      _active = -1;
      return;
    }

    if (_active >= 0 && _active < _count && _entries[_active].app) _entries[_active].app->loop();
  }

private:
  struct Entry { const char* group; const char* name; App* app; };

  InputManager& _in;
  UIEngine& _ui;
  SDManager& _sd;
  AppState _state;
  Entry _entries[MAX_APPS];
  uint8_t _count;
  uint8_t _sel;
  int8_t _active;
  bool _showGroupOnly;

  // Apps
  SettingsApp _settings{_in, _ui};
  SimpleInfoApp _fileMgr{_in, _ui, "File Manager", "SELECT open file", "MENU delete item"};
  SystemMonitorApp _sysMon{_in, _ui, _sd};
  SimpleInfoApp _launcherInfo{_in, _ui, "Launcher", "UP/DOWN browse", "SELECT launch"};

  SnakeApp _snake{_in, _ui};
  PongApp _pong{_in, _ui};
  TicTacToeApp _ttt{_in, _ui};

  SimpleInfoApp _wifi{_in, _ui, "WiFi Scanner", "MENU scan APs", "SELECT details"};
  SimpleInfoApp _ble{_in, _ui, "BLE Scanner", "MENU scan BLE", "SELECT details"};
  SimpleInfoApp _signal{_in, _ui, "Signal Monitor", "UP/DOWN channel", "MENU refresh"};

  SimpleInfoApp _graph{_in, _ui, "Graphing Calc", "L/R zoom", "U/D pan"};
  SimpleInfoApp _chem{_in, _ui, "Chemistry App", "SELECT element", "MENU periodic"};
  LightToolApp _light{_in, _ui};

  void add(const char* group, const char* name, App* app) {
    if (_count >= MAX_APPS) return;
    _entries[_count].group = group;
    _entries[_count].name = name;
    _entries[_count].app = app;
    _count++;
  }

  void initRegistry() {
    add("Core", "Settings", &_settings);
    add("Core", "File Manager", &_fileMgr);
    add("Core", "System Monitor", &_sysMon);
    add("Core", "Launcher", &_launcherInfo);

    add("Games", "Snake", &_snake);
    add("Games", "Pong", &_pong);
    add("Games", "Tic Tac Toe", &_ttt);

    add("Network", "WiFi Scanner", &_wifi);
    add("Network", "BLE Scanner", &_ble);
    add("Network", "Signal Monitor", &_signal);

    add("Utilities", "Graphing Calculator", &_graph);
    add("Utilities", "Chemistry App", &_chem);
    add("Utilities", "Light Tool", &_light);
  }

  void handleLauncher(InputEvent e) {
    if (e == EVENT_UP && _sel > 0) _sel--;
    else if (e == EVENT_DOWN && _sel + 1 < _count) _sel++;
    else if (e == EVENT_MENU) _showGroupOnly = !_showGroupOnly;
    else if (e == EVENT_SELECT && _entries[_sel].app) {
      _active = (int8_t)_sel;
      _state = APPSTATE_RUNNING;
      _entries[_active].app->setup();
    }
  }

  void drawLauncher() {
    static char labels[MAX_APPS][29];
    const char* lines[MAX_APPS];
    for (uint8_t i = 0; i < _count; i++) {
      if (_showGroupOnly) snprintf(labels[i], sizeof(labels[i]), "%s", _entries[i].group);
      else snprintf(labels[i], sizeof(labels[i]), "%s/%s", _entries[i].group, _entries[i].name);
      lines[i] = labels[i];
    }

    char info[30];
    snprintf(info, sizeof(info), "%s  (%u/%u)", _entries[_sel].name, (unsigned)(_sel + 1), (unsigned)_count);

    _ui.clear();
    _ui.drawHeader("PigeonOS Launcher");
    _ui.drawMenu(lines, _count, _sel);
    _ui.drawText(2, 54, info);
    _ui.drawFooter("SEL open", "MENU view");
  }
};

// Kernel singletons
static InputManager inputMgr;
static UIEngine ui;
static SDManager sd;
static AppLoader loader(inputMgr, ui, sd);
static uint32_t s_lastTick = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {}

  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, LOW);

  ui.begin();
  ui.clear();
  ui.drawHeader("PigeonOS");
  ui.drawText(26, 24, "Native App OS");
  ui.drawFooter("Booting", "ESP32");
  ui.render();

  sd.begin();
  inputMgr.begin();
  loader.begin();
  s_lastTick = millis();
}

void loop() {
  uint32_t now = millis();
  if ((now - s_lastTick) < (uint32_t)KERNEL_TICK_MS) return;
  s_lastTick = now;

  inputMgr.poll();
  loader.update();
  ui.render();
}
