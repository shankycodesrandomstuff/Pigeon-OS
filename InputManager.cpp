#include "InputManager.h"

// Map index → pin and index → event in the same order
static const uint8_t PIN_MAP[7] = {
    PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT,
    PIN_BTN_SELECT, PIN_BTN_BACK, PIN_BTN_MENU
};

static const InputEvent EVENT_MAP[7] = {
    EVENT_UP, EVENT_DOWN, EVENT_LEFT, EVENT_RIGHT,
    EVENT_SELECT, EVENT_BACK, EVENT_MENU
};

void InputManager::begin() {
    _qHead = 0;
    _qTail = 0;
    _qCount = 0;
    _now = 0;

    for (uint8_t i = 0; i < 7; i++) {
        _buttons[i].pin          = PIN_MAP[i];
        _buttons[i].lastRaw      = false;   // not pressed
        _buttons[i].stable       = false;
        _buttons[i].lastChangeMs = 0;
        _buttons[i].pressStartMs = 0;
        _buttons[i].lastRepeatMs = 0;
        _buttons[i].repeatPhase  = 0;
        // GPIO34/35 have no internal pull-up; still call pinMode for others.
        // External pull-ups required on those two pins.
        pinMode(PIN_MAP[i], INPUT_PULLUP);
    }
}

void InputManager::_enqueue(InputEvent e) {
    if (_qCount >= INPUT_QUEUE_SIZE) {
        // Drop oldest to make room
        _qHead = (_qHead + 1) % INPUT_QUEUE_SIZE;
        _qCount--;
    }
    _queue[_qTail] = e;
    _qTail = (_qTail + 1) % INPUT_QUEUE_SIZE;
    _qCount++;
}

void InputManager::_processButton(ButtonState& b, InputEvent e) {
    // LOW = pressed (INPUT_PULLUP)
    bool raw = (digitalRead(b.pin) == LOW);

    // Detect raw state change to start debounce timer
    if (raw != b.lastRaw) {
        b.lastChangeMs = _now;
        b.lastRaw = raw;
    }

    // Reject anything inside the debounce window
    if ((_now - b.lastChangeMs) < INPUT_DEBOUNCE_MS) {
        return;
    }

    if (raw && !b.stable) {
        // Debounced press — fire immediately
        b.stable       = true;
        b.pressStartMs = _now;
        b.lastRepeatMs = _now;
        b.repeatPhase  = 0;
        _enqueue(e);

    } else if (!raw && b.stable) {
        // Debounced release
        b.stable      = false;
        b.repeatPhase = 0;

    } else if (raw && b.stable) {
        // Held — determine repeat phase and interval
        uint32_t held = _now - b.pressStartMs;
        uint32_t interval;

        if (held >= INPUT_HOLD_FAST_MS) {
            b.repeatPhase = 2;
            interval = INPUT_REPEAT_FAST_MS;
        } else if (held >= INPUT_HOLD_INITIAL_MS) {
            if (b.repeatPhase < 2) b.repeatPhase = 1;
            interval = INPUT_REPEAT_NORMAL_MS;
        } else {
            return; // Still in dead zone before first repeat
        }

        if ((_now - b.lastRepeatMs) >= interval) {
            b.lastRepeatMs = _now;
            _enqueue(e);
        }
    }
}

void InputManager::poll() {
    _now = millis();
    for (uint8_t i = 0; i < 7; i++) {
        _processButton(_buttons[i], EVENT_MAP[i]);
    }
}

bool InputManager::hasEvent() const {
    return _qCount > 0;
}

InputEvent InputManager::peekEvent() const {
    if (_qCount == 0) return EVENT_NONE;
    return _queue[_qHead];
}

InputEvent InputManager::getEvent() {
    if (_qCount == 0) return EVENT_NONE;
    InputEvent e = _queue[_qHead];
    _qHead = (_qHead + 1) % INPUT_QUEUE_SIZE;
    _qCount--;
    return e;
}
