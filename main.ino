// ─────────────────────────────────────────────────────────────────────────────
//  PigeonOS — single-sketch build (ESP32 / Arduino)
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <U8g2lib.h>

// ─── Button Pins (INPUT_PULLUP, pressed = LOW) ───────────────────────────────
#define PIN_BTN_UP      34
#define PIN_BTN_DOWN    35
#define PIN_BTN_LEFT    32
#define PIN_BTN_RIGHT   33
#define PIN_BTN_SELECT  25
#define PIN_BTN_BACK    26
#define PIN_BTN_MENU    27

// ─── Display (I2C) ───────────────────────────────────────────────────────────
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// ─── SD Card (SPI) ───────────────────────────────────────────────────────────
#define SD_CS           5
#define SD_MOSI         23
#define SD_MISO         19
#define SD_SCK          18

// ─── Misc / timing ────────────────────────────────────────────────────────────
#define BUILTIN_LED     2
#define KERNEL_TICK_MS  10
#define INPUT_DEBOUNCE_MS       ((uint32_t)20)
#define INPUT_HOLD_INITIAL_MS   ((uint32_t)300)
#define INPUT_HOLD_FAST_MS      ((uint32_t)800)
#define INPUT_REPEAT_NORMAL_MS  ((uint32_t)150)
#define INPUT_REPEAT_FAST_MS    ((uint32_t)70)
#define INPUT_QUEUE_SIZE        10
#define MAX_APPS                8

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
        for (uint8_t i = 0; i < 7; i++) {
            _processButton(_buttons[i], EVENT_MAP[i]);
        }
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
    InputEvent  _queue[INPUT_QUEUE_SIZE];
    uint8_t     _qHead;
    uint8_t     _qTail;
    uint8_t     _qCount;
    uint32_t    _now;

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
                b.repeatPhase = 2;
                interval = INPUT_REPEAT_FAST_MS;
            } else if (held >= INPUT_HOLD_INITIAL_MS) {
                if (b.repeatPhase < 2) b.repeatPhase = 1;
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
        _u8g2 = &s_display;
        _dirty = false;

        _u8g2->begin();
        _u8g2->setFont(u8g2_font_6x10_tf);
        _u8g2->setFontRefHeightExtendedText();
        _u8g2->setFontPosTop();
        _u8g2->setDrawColor(1);
        _u8g2->clearBuffer();
        return true;
    }

    void clear() {
        _u8g2->clearBuffer();
        _dirty = true;
    }

    void render() {
        if (_dirty) {
            _u8g2->sendBuffer();
            _dirty = false;
        }
    }

    void drawText(uint8_t x, uint8_t y, const char* text) {
        if (!text) return;
        _u8g2->drawStr(x, y, text);
        _dirty = true;
    }

    void drawMenu(const char* const* items, uint8_t count, uint8_t selectedIndex) {
        if (!items || count == 0) return;

        const uint8_t itemH = 12;
        const uint8_t visRows = OLED_HEIGHT / itemH;
        uint8_t scrollTop = 0;
        if (selectedIndex >= visRows) scrollTop = (uint8_t)(selectedIndex - visRows + 1);

        for (uint8_t i = 0; i < visRows; i++) {
            uint8_t idx = (uint8_t)(scrollTop + i);
            if (idx >= count) break;
            uint8_t rowY = (uint8_t)(i * itemH);

            if (idx == selectedIndex) {
                _u8g2->setDrawColor(1);
                _u8g2->drawBox(0, rowY, OLED_WIDTH, itemH);
                _u8g2->setDrawColor(0);
                _u8g2->drawStr(4, rowY + 1, items[idx]);
                _u8g2->setDrawColor(1);
            } else {
                _u8g2->drawStr(4, rowY + 1, items[idx]);
            }
        }

        _dirty = true;
    }

    void drawStatusBar(const char* left, const char* right) {
        const uint8_t barY = OLED_HEIGHT - 10;
        _u8g2->drawHLine(0, barY, OLED_WIDTH);
        if (left) _u8g2->drawStr(2, barY + 1, left);
        if (right) {
            uint16_t w = _u8g2->getStrWidth(right);
            if (w < OLED_WIDTH - 4) _u8g2->drawStr((uint8_t)(OLED_WIDTH - w - 2), barY + 1, right);
        }
        _dirty = true;
    }

private:
    static U8G2_SH1106_128X64_NONAME_F_HW_I2C s_display;
    U8G2_SH1106_128X64_NONAME_F_HW_I2C* _u8g2;
    bool _dirty;
};

U8G2_SH1106_128X64_NONAME_F_HW_I2C UIEngine::s_display(U8G2_R0, U8X8_PIN_NONE);

class SDManager {
public:
    SDManager() : _ready(false) {}

    bool begin() {
        SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
        _ready = SD.begin(SD_CS);
        if (!_ready) Serial.println("[SDManager] SD.begin() failed");
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

static const char* eventToText(InputEvent e) {
    switch (e) {
        case EVENT_UP: return "UP";
        case EVENT_DOWN: return "DOWN";
        case EVENT_LEFT: return "LEFT";
        case EVENT_RIGHT: return "RIGHT";
        case EVENT_SELECT: return "SELECT";
        case EVENT_BACK: return "BACK";
        case EVENT_MENU: return "MENU";
        default: return nullptr;
    }
}

class ButtonEchoApp : public App {
public:
    ButtonEchoApp(InputManager& input, UIEngine& ui) : _input(input), _ui(ui) {
        strncpy(_lastEvent, "none", sizeof(_lastEvent) - 1);
        _lastEvent[sizeof(_lastEvent) - 1] = '\0';
    }

    void setup() override {
        strncpy(_lastEvent, "none", sizeof(_lastEvent) - 1);
        _lastEvent[sizeof(_lastEvent) - 1] = '\0';
    }

    void loop() override {
        while (_input.hasEvent()) {
            InputEvent e = _input.getEvent();
            const char* label = eventToText(e);
            if (label) {
                strncpy(_lastEvent, label, sizeof(_lastEvent) - 1);
                _lastEvent[sizeof(_lastEvent) - 1] = '\0';
            }
        }

        _ui.clear();
        _ui.drawText(4, 8, "Button Echo");
        _ui.drawText(4, 24, "Last event:");
        _ui.drawText(4, 36, _lastEvent);
        _ui.drawStatusBar("BACK=exit", "");
    }

private:
    InputManager& _input;
    UIEngine& _ui;
    char _lastEvent[20];
};

class SDStatusApp : public App {
public:
    SDStatusApp(InputManager& input, UIEngine& ui, SDManager& sd)
        : _input(input), _ui(ui), _sd(sd), _eventCount(0) {}

    void setup() override { _eventCount = 0; }

    void loop() override {
        while (_input.hasEvent()) {
            _input.getEvent();
            _eventCount++;
        }

        char line[22];
        snprintf(line, sizeof(line), "Events: %u", (unsigned)_eventCount);

        _ui.clear();
        _ui.drawText(4, 8, "SD Status");
        _ui.drawText(4, 24, _sd.isReady() ? "SD: ready" : "SD: not ready");
        _ui.drawText(4, 36, line);
        _ui.drawStatusBar("BACK=exit", "");
    }

private:
    InputManager& _input;
    UIEngine& _ui;
    SDManager& _sd;
    uint16_t _eventCount;
};

enum AppState : uint8_t {
    APPSTATE_LAUNCHER = 0,
    APPSTATE_RUNNING
};

class AppLoader {
public:
    AppLoader(InputManager& input, UIEngine& ui, SDManager& sd)
        : _input(input), _ui(ui), _sd(sd), _state(APPSTATE_LAUNCHER),
          _appCount(0), _selectedIdx(0), _activeAppIdx(-1)
    {
        for (uint8_t i = 0; i < MAX_APPS; i++) {
            _entries[i].name = nullptr;
            _entries[i].app = nullptr;
        }
    }

    void begin() { _initRegistry(); }

    void update() {
        if (_state == APPSTATE_LAUNCHER) {
            while (_input.hasEvent()) _handleLauncherEvent(_input.getEvent());
            _drawLauncher();
            return;
        }

        if (_input.peekEvent() == EVENT_BACK) {
            _input.getEvent();
            _exitApp();
            return;
        }

        if (_activeAppIdx >= 0 && _activeAppIdx < _appCount && _entries[_activeAppIdx].app) {
            _entries[_activeAppIdx].app->loop();
        }
    }

private:
    struct AppEntry {
        const char* name;
        App* app;
    };

    InputManager& _input;
    UIEngine& _ui;
    SDManager& _sd;

    AppState _state;
    AppEntry _entries[MAX_APPS];
    uint8_t _appCount;
    uint8_t _selectedIdx;
    int8_t _activeAppIdx;

    void _initRegistry() {
        static ButtonEchoApp buttonEchoApp(_input, _ui);
        static SDStatusApp sdStatusApp(_input, _ui, _sd);

        _entries[0].name = "Button Echo";
        _entries[0].app = &buttonEchoApp;
        _entries[1].name = "SD Status";
        _entries[1].app = &sdStatusApp;
        _appCount = 2;

        Serial.print("[AppLoader] Registered ");
        Serial.print(_appCount);
        Serial.println(" native app(s)");
    }

    void _launchApp(uint8_t idx) {
        if (idx >= _appCount || !_entries[idx].app) return;
        _activeAppIdx = (int8_t)idx;
        _state = APPSTATE_RUNNING;
        _entries[idx].app->setup();
        Serial.print("[AppLoader] Running: ");
        Serial.println(_entries[idx].name);
    }

    void _exitApp() {
        _activeAppIdx = -1;
        _state = APPSTATE_LAUNCHER;
        _selectedIdx = 0;
    }

    void _drawLauncher() {
        _ui.clear();
        if (_appCount == 0) {
            _ui.drawText(4, 16, "No apps registered");
        } else {
            const char* names[MAX_APPS];
            for (uint8_t i = 0; i < _appCount; i++) names[i] = _entries[i].name;
            _ui.drawMenu(names, _appCount, _selectedIdx);
        }
        _ui.drawStatusBar("PigeonOS", "SELECT=open");
    }

    void _handleLauncherEvent(InputEvent e) {
        switch (e) {
            case EVENT_UP:
                if (_selectedIdx > 0) _selectedIdx--;
                break;
            case EVENT_DOWN:
                if (_selectedIdx + 1 < _appCount) _selectedIdx++;
                break;
            case EVENT_SELECT:
                if (_appCount > 0) _launchApp(_selectedIdx);
                break;
            default:
                break;
        }
    }
};

// ─── Kernel singletons ────────────────────────────────────────────────────────
static InputManager inputMgr;
static UIEngine     uiEngine;
static SDManager    sdMgr;
static AppLoader    appLoader(inputMgr, uiEngine, sdMgr);
static uint32_t s_lastTick = 0;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {}
    Serial.println("\n[PigeonOS] Booting...");

    pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(BUILTIN_LED, LOW);

    if (!uiEngine.begin()) {
        Serial.println("[UIEngine] OLED init FAILED");
    } else {
        uiEngine.clear();
        uiEngine.drawText(20, 18, "PigeonOS");
        uiEngine.drawText(8, 32, "single-sketch");
        uiEngine.render();
    }

    if (!sdMgr.begin()) {
        Serial.println("[SDManager] SD init FAILED");
        uiEngine.clear();
        uiEngine.drawText(4, 18, "SD card error!");
        uiEngine.drawText(4, 30, "Check wiring.");
        uiEngine.render();
        delay(1200);
    }

    inputMgr.begin();
    appLoader.begin();

    s_lastTick = millis();
    Serial.println("[PigeonOS] Boot complete");
}

void loop() {
    uint32_t now = millis();
    if ((now - s_lastTick) < (uint32_t)KERNEL_TICK_MS) return;
    s_lastTick = now;

    inputMgr.poll();
    appLoader.update();
    uiEngine.render();
}
