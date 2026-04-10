#include "NativeApps.h"

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

ButtonEchoApp::ButtonEchoApp(InputManager& input, UIEngine& ui)
    : _input(input), _ui(ui)
{
    strncpy(_lastEvent, "none", sizeof(_lastEvent) - 1);
    _lastEvent[sizeof(_lastEvent) - 1] = '\0';
}

const char* ButtonEchoApp::name() const {
    return "Button Echo";
}

void ButtonEchoApp::setup() {
    strncpy(_lastEvent, "none", sizeof(_lastEvent) - 1);
    _lastEvent[sizeof(_lastEvent) - 1] = '\0';
}

void ButtonEchoApp::loop() {
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

SDStatusApp::SDStatusApp(InputManager& input, UIEngine& ui, SDManager& sd)
    : _input(input), _ui(ui), _sd(sd), _eventCount(0)
{}

const char* SDStatusApp::name() const {
    return "SD Status";
}

void SDStatusApp::setup() {
    _eventCount = 0;
}

void SDStatusApp::loop() {
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
