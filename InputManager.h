#pragma once
#include <Arduino.h>
#include "Config.h"

// All button events. EVENT_NONE = no event pending.
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
    void begin();
    void poll();
    bool hasEvent() const;
    InputEvent peekEvent() const;
    InputEvent getEvent();

private:
    struct ButtonState {
        uint8_t  pin;
        bool     lastRaw;
        bool     stable;
        uint32_t lastChangeMs;
        uint32_t pressStartMs;
        uint32_t lastRepeatMs;
        uint8_t  repeatPhase;   // 0=idle, 1=normal repeat, 2=fast repeat
    };

    ButtonState _buttons[7];
    InputEvent  _queue[INPUT_QUEUE_SIZE];
    uint8_t     _qHead;
    uint8_t     _qTail;
    uint8_t     _qCount;
    uint32_t    _now;

    void _enqueue(InputEvent e);
    void _processButton(ButtonState& b, InputEvent e);
};
