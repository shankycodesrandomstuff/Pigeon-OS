#include "AppLoader.h"
#include "NativeApps.h"

AppLoader::AppLoader(InputManager& input, UIEngine& ui, SDManager& sd)
    : _input(input), _ui(ui), _sd(sd), _state(APPSTATE_LAUNCHER),
      _appCount(0), _selectedIdx(0), _activeAppIdx(-1)
{
    for (uint8_t i = 0; i < MAX_APPS; i++) {
        _apps[i] = nullptr;
        _appNames[i] = nullptr;
    }
}

void AppLoader::begin() {
    _initRegistry();
}

void AppLoader::_initRegistry() {
    static ButtonEchoApp buttonEchoApp(_input, _ui);
    static SDStatusApp sdStatusApp(_input, _ui, _sd);

    _apps[0] = &buttonEchoApp;
    _apps[1] = &sdStatusApp;
    _appCount = 2;

    for (uint8_t i = 0; i < _appCount; i++) {
        _appNames[i] = _apps[i]->name();
    }

    Serial.print("[AppLoader] Registered ");
    Serial.print(_appCount);
    Serial.println(" native app(s)");
}

void AppLoader::_launchApp(uint8_t idx) {
    if (idx >= _appCount || !_apps[idx]) return;

    _activeAppIdx = (int8_t)idx;
    _state = APPSTATE_RUNNING;
    _apps[idx]->setup();

    Serial.print("[AppLoader] Running: ");
    Serial.println(_appNames[idx]);
}

void AppLoader::_exitApp() {
    _activeAppIdx = -1;
    _state = APPSTATE_LAUNCHER;
    _selectedIdx = 0;
}

void AppLoader::_drawLauncher() {
    _ui.clear();
    if (_appCount == 0) {
        _ui.drawText(4, 16, "No apps registered");
    } else {
        _ui.drawMenu(_appNames, _appCount, _selectedIdx);
    }
    _ui.drawStatusBar("PigeonOS", "SELECT=open");
}

void AppLoader::_handleLauncherEvent(InputEvent e) {
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

void AppLoader::update() {
    if (_state == APPSTATE_LAUNCHER) {
        while (_input.hasEvent()) {
            _handleLauncherEvent(_input.getEvent());
        }
        _drawLauncher();
        return;
    }

    if (_input.peekEvent() == EVENT_BACK) {
        _input.getEvent();
        _exitApp();
        return;
    }

    if (_activeAppIdx >= 0 && _activeAppIdx < _appCount && _apps[_activeAppIdx]) {
        _apps[_activeAppIdx]->loop();
    }
}
