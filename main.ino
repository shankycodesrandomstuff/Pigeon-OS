#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <U8g2lib.h>

// Pins
#define PIN_BTN_UP      34
#define PIN_BTN_DOWN    35
#define PIN_BTN_LEFT    32
#define PIN_BTN_RIGHT   33
#define PIN_BTN_SELECT  25
#define PIN_BTN_BACK    26
#define PIN_BTN_MENU    27

// Display
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// SD
#define SD_CS           5
#define SD_MOSI         23
#define SD_MISO         19
#define SD_SCK          18

// Kernel/input
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
            _buttons[i].repeatPhase = 0;
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
        uint8_t  pin;
        bool     lastRaw;
        bool     stable;
        uint32_t lastChangeMs;
        uint32_t pressStartMs;
        uint32_t lastRepeatMs;
        uint8_t  repeatPhase;
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
            b.repeatPhase = 0;
            _enqueue(e);
        } else if (!raw && b.stable) {
            b.stable = false;
            b.repeatPhase = 0;
        } else if (raw && b.stable) {
            uint32_t held = _now - b.pressStartMs;
            uint32_t interval;
            if (held >= INPUT_HOLD_FAST_MS) {
                interval = INPUT_REPEAT_FAST_MS;
            } else if (held >= INPUT_HOLD_INITIAL_MS) {
                interval = INPUT_REPEAT_NORMAL_MS;
            } else {
                return;
            }
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

    void drawText(uint8_t x, uint8_t y, const char* txt) {
        if (!txt) return;
        _u8->drawStr(x, y, txt);
        _dirty = true;
    }

    void drawFrame(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
        _u8->drawFrame(x, y, w, h);
        _dirty = true;
    }

    void drawBox(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
        _u8->drawBox(x, y, w, h);
        _dirty = true;
    }

    void drawPixel(uint8_t x, uint8_t y) {
        _u8->drawPixel(x, y);
        _dirty = true;
    }

    void drawMenu(const char* const* items, uint8_t count, uint8_t selected) {
        const uint8_t itemH = 11;
        const uint8_t visRows = 5;
        uint8_t top = (selected >= visRows) ? (uint8_t)(selected - visRows + 1) : 0;

        for (uint8_t i = 0; i < visRows; i++) {
            uint8_t idx = (uint8_t)(top + i);
            if (idx >= count) break;
            uint8_t y = (uint8_t)(i * itemH + 1);
            if (idx == selected) {
                _u8->drawBox(0, y, OLED_WIDTH, itemH);
                _u8->setDrawColor(0);
                _u8->drawStr(2, y + 1, items[idx]);
                _u8->setDrawColor(1);
            } else {
                _u8->drawStr(2, y + 1, items[idx]);
            }
        }
        _dirty = true;
    }

    void drawStatusBar(const char* left, const char* right) {
        const uint8_t y = OLED_HEIGHT - 10;
        _u8->drawHLine(0, y, OLED_WIDTH);
        if (left) _u8->drawStr(1, y + 1, left);
        if (right) {
            uint16_t w = _u8->getStrWidth(right);
            if (w < OLED_WIDTH) _u8->drawStr((uint8_t)(OLED_WIDTH - w - 1), y + 1, right);
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

// Shared helpers
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
    SimpleInfoApp(InputManager& in, UIEngine& ui, const char* title, const char* line1, const char* line2)
        : _input(in), _ui(ui), _title(title), _line1(line1), _line2(line2)
    {
        strcpy(_last, "NONE");
    }

    void setup() override { strcpy(_last, "NONE"); }

    void loop() override {
        while (_input.hasEvent()) {
            InputEvent e = _input.getEvent();
            strncpy(_last, eventText(e), sizeof(_last) - 1);
            _last[sizeof(_last) - 1] = '\0';
        }
        _ui.clear();
        _ui.drawText(2, 1, _title);
        _ui.drawText(2, 14, _line1);
        _ui.drawText(2, 26, _line2);
        _ui.drawText(2, 38, "Last:");
        _ui.drawText(34, 38, _last);
        _ui.drawStatusBar("BACK=exit", "MENU=alt");
    }

private:
    InputManager& _input;
    UIEngine& _ui;
    const char* _title;
    const char* _line1;
    const char* _line2;
    char _last[10];
};

class SnakeApp : public App {
public:
    SnakeApp(InputManager& in, UIEngine& ui) : _in(in), _ui(ui) {}

    void setup() override {
        _x = 8; _y = 4; _dx = 1; _dy = 0; _foodX = 3; _foodY = 3; _score = 0;
    }

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

        if (millis() - _lastStep > 180) {
            _lastStep = millis();
            _x = (uint8_t)((_x + _dx + 16) % 16);
            _y = (uint8_t)((_y + _dy + 8) % 8);
            if (_x == _foodX && _y == _foodY) {
                _score++;
                _foodX = (uint8_t)((_foodX + 5) % 16);
                _foodY = (uint8_t)((_foodY + 3) % 8);
            }
        }

        _ui.clear();
        _ui.drawFrame(0, 0, 96, 48);
        _ui.drawBox((uint8_t)(2 + _x * 5), (uint8_t)(2 + _y * 5), 4, 4);
        _ui.drawFrame((uint8_t)(2 + _foodX * 5), (uint8_t)(2 + _foodY * 5), 4, 4);
        char s[16]; snprintf(s, sizeof(s), "Score:%u", _score);
        _ui.drawText(98, 2, "Snake");
        _ui.drawText(98, 14, s);
        _ui.drawStatusBar("Arrows", "SEL=reset");
    }

private:
    InputManager& _in;
    UIEngine& _ui;
    uint8_t _x, _y, _foodX, _foodY;
    int8_t _dx, _dy;
    uint8_t _score;
    uint32_t _lastStep;
};

class PongApp : public App {
public:
    PongApp(InputManager& in, UIEngine& ui) : _in(in), _ui(ui) {}

    void setup() override {
        _paddleX = 50; _ballX = 40; _ballY = 20; _vx = 1; _vy = 1; _score = 0;
    }

    void loop() override {
        while (_in.hasEvent()) {
            InputEvent e = _in.getEvent();
            if (e == EVENT_LEFT && _paddleX > 2) _paddleX -= 3;
            else if (e == EVENT_RIGHT && _paddleX < 80) _paddleX += 3;
            else if (e == EVENT_SELECT) setup();
        }

        if (millis() - _lastStep > 70) {
            _lastStep = millis();
            _ballX += _vx; _ballY += _vy;
            if (_ballX <= 1 || _ballX >= 93) _vx = (int8_t)-_vx;
            if (_ballY <= 1) _vy = (int8_t)-_vy;
            if (_ballY >= 42) {
                if (_ballX >= _paddleX && _ballX <= _paddleX + 16) {
                    _vy = -1;
                    _score++;
                } else {
                    setup();
                }
            }
        }

        char s[16]; snprintf(s, sizeof(s), "Score:%u", _score);
        _ui.clear();
        _ui.drawFrame(0, 0, 96, 48);
        _ui.drawBox((uint8_t)_paddleX, 44, 16, 2);
        _ui.drawBox((uint8_t)_ballX, (uint8_t)_ballY, 2, 2);
        _ui.drawText(98, 2, "Pong");
        _ui.drawText(98, 14, s);
        _ui.drawStatusBar("L/R move", "SEL=reset");
    }

private:
    InputManager& _in;
    UIEngine& _ui;
    int16_t _paddleX, _ballX, _ballY;
    int8_t _vx, _vy;
    uint8_t _score;
    uint32_t _lastStep;
};

class TicTacToeApp : public App {
public:
    TicTacToeApp(InputManager& in, UIEngine& ui) : _in(in), _ui(ui) {}

    void setup() override {
        for (uint8_t i = 0; i < 9; i++) _cells[i] = 0;
        _cursor = 0;
        _player = 1;
        _winner = 0;
    }

    void loop() override {
        while (_in.hasEvent()) {
            InputEvent e = _in.getEvent();
            if (e == EVENT_UP && _cursor >= 3) _cursor -= 3;
            else if (e == EVENT_DOWN && _cursor <= 5) _cursor += 3;
            else if (e == EVENT_LEFT && (_cursor % 3) > 0) _cursor--;
            else if (e == EVENT_RIGHT && (_cursor % 3) < 2) _cursor++;
            else if (e == EVENT_SELECT) {
                if (_winner) { setup(); break; }
                if (_cells[_cursor] == 0) {
                    _cells[_cursor] = _player;
                    _player = (uint8_t)(3 - _player);
                    _winner = _checkWinner();
                }
            }
            else if (e == EVENT_MENU) setup();
        }

        _ui.clear();
        _ui.drawText(0, 0, "TicTacToe");
        for (uint8_t r = 0; r < 3; r++) {
            for (uint8_t c = 0; c < 3; c++) {
                uint8_t idx = (uint8_t)(r * 3 + c);
                uint8_t x = (uint8_t)(4 + c * 20);
                uint8_t y = (uint8_t)(12 + r * 14);
                _ui.drawFrame(x, y, 18, 12);
                if (idx == _cursor) _ui.drawFrame((uint8_t)(x - 1), (uint8_t)(y - 1), 20, 14);
                if (_cells[idx] == 1) _ui.drawText((uint8_t)(x + 6), (uint8_t)(y + 1), "X");
                else if (_cells[idx] == 2) _ui.drawText((uint8_t)(x + 6), (uint8_t)(y + 1), "O");
            }
        }

        if (_winner == 1) _ui.drawText(72, 18, "X wins");
        else if (_winner == 2) _ui.drawText(72, 18, "O wins");
        else _ui.drawText(72, 18, _player == 1 ? "Turn X" : "Turn O");
        _ui.drawStatusBar("Arrows+SEL", "MENU=reset");
    }

private:
    uint8_t _checkWinner() {
        static const uint8_t lines[8][3] = {
            {0,1,2},{3,4,5},{6,7,8},{0,3,6},
            {1,4,7},{2,5,8},{0,4,8},{2,4,6}
        };
        for (uint8_t i = 0; i < 8; i++) {
            uint8_t a = lines[i][0], b = lines[i][1], c = lines[i][2];
            if (_cells[a] && _cells[a] == _cells[b] && _cells[a] == _cells[c]) return _cells[a];
        }
        return 0;
    }

    InputManager& _in;
    UIEngine& _ui;
    uint8_t _cells[9];
    uint8_t _cursor;
    uint8_t _player;
    uint8_t _winner;
};

enum AppState : uint8_t { APPSTATE_LAUNCHER = 0, APPSTATE_RUNNING };

class AppLoader {
public:
    AppLoader(InputManager& input, UIEngine& ui, SDManager& sd)
        : _input(input), _ui(ui), _sd(sd), _state(APPSTATE_LAUNCHER),
          _appCount(0), _selected(0), _active(-1)
    {
        for (uint8_t i = 0; i < MAX_APPS; i++) {
            _entries[i].category = nullptr;
            _entries[i].name = nullptr;
            _entries[i].app = nullptr;
        }
    }

    void begin() { _initRegistry(); }

    void update() {
        if (_state == APPSTATE_LAUNCHER) {
            while (_input.hasEvent()) _handleLauncher(_input.getEvent());
            _drawLauncher();
            return;
        }

        if (_input.peekEvent() == EVENT_BACK) {
            _input.getEvent();
            _state = APPSTATE_LAUNCHER;
            _active = -1;
            return;
        }

        if (_active >= 0 && _active < _appCount && _entries[_active].app) {
            _entries[_active].app->loop();
        }
    }

private:
    struct Entry {
        const char* category;
        const char* name;
        App* app;
    };

    InputManager& _input;
    UIEngine& _ui;
    SDManager& _sd;
    AppState _state;
    Entry _entries[MAX_APPS];
    uint8_t _appCount;
    uint8_t _selected;
    int8_t _active;

    // App instances
    SimpleInfoApp _settings{_input, _ui, "Settings", "UP/DOWN volume", "MENU save cfg"};
    SimpleInfoApp _fileMgr{_input, _ui, "File Manager", "SELECT open", "MENU delete"};
    SimpleInfoApp _sysMon{_input, _ui, "System Monitor", "Shows uptime", "& free heap"};
    SimpleInfoApp _launcherInfo{_input, _ui, "Launcher", "Use arrows", "SELECT launch"};

    SnakeApp _snake{_input, _ui};
    PongApp _pong{_input, _ui};
    TicTacToeApp _ttt{_input, _ui};

    SimpleInfoApp _wifiScan{_input, _ui, "WiFi Scanner", "MENU scan", "SELECT detail"};
    SimpleInfoApp _bleScan{_input, _ui, "BLE Scanner", "MENU scan", "SELECT detail"};
    SimpleInfoApp _signalMon{_input, _ui, "Signal Monitor", "UP/DOWN chan", "MENU refresh"};

    SimpleInfoApp _graphCalc{_input, _ui, "Graph Calculator", "L/R zoom", "U/D pan"};
    SimpleInfoApp _chem{_input, _ui, "Chemistry App", "SELECT lookup", "MENU table"};
    SimpleInfoApp _light{_input, _ui, "Light Tool", "UP/DOWN bright", "MENU strobe"};

    void _add(const char* cat, const char* name, App* app) {
        if (_appCount >= MAX_APPS) return;
        _entries[_appCount].category = cat;
        _entries[_appCount].name = name;
        _entries[_appCount].app = app;
        _appCount++;
    }

    void _initRegistry() {
        _add("Core", "Settings", &_settings);
        _add("Core", "File Manager", &_fileMgr);
        _add("Core", "System Monitor", &_sysMon);
        _add("Core", "Launcher", &_launcherInfo);

        _add("Games", "Snake", &_snake);
        _add("Games", "Pong", &_pong);
        _add("Games", "Tic Tac Toe", &_ttt);

        _add("Network", "WiFi Scanner", &_wifiScan);
        _add("Network", "BLE Scanner", &_bleScan);
        _add("Network", "Signal Monitor", &_signalMon);

        _add("Utilities", "Graphing Calculator", &_graphCalc);
        _add("Utilities", "Chemistry App", &_chem);
        _add("Utilities", "Light Tool", &_light);

        Serial.print("[Launcher] apps: ");
        Serial.println(_appCount);
    }

    void _handleLauncher(InputEvent e) {
        if (e == EVENT_UP && _selected > 0) _selected--;
        else if (e == EVENT_DOWN && _selected + 1 < _appCount) _selected++;
        else if (e == EVENT_SELECT && _entries[_selected].app) {
            _active = (int8_t)_selected;
            _state = APPSTATE_RUNNING;
            _entries[_active].app->setup();
        }
    }

    void _drawLauncher() {
        const char* lines[MAX_APPS];
        static char labels[MAX_APPS][28];
        for (uint8_t i = 0; i < _appCount; i++) {
            snprintf(labels[i], sizeof(labels[i]), "%s/%s", _entries[i].category, _entries[i].name);
            lines[i] = labels[i];
        }
        _ui.clear();
        _ui.drawMenu(lines, _appCount, _selected);
        _ui.drawStatusBar("SELECT=open", "BACK=exit app");
    }
};

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
    ui.drawText(16, 18, "PigeonOS");
    ui.drawText(6, 32, "Native App Suite");
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
