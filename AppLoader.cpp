#include "AppLoader.h"

AppLoader::AppLoader(InputManager& input, UIEngine& ui,
                     SDManager& sd, LuaEngine& lua)
    : _input(input), _ui(ui), _sd(sd), _lua(lua),
      _state(APPSTATE_LAUNCHER), _appCount(0), _selectedIdx(0)
{
    memset(_appNames, 0, sizeof(_appNames));
    memset(_appPtrs,  0, sizeof(_appPtrs));
    memset(_scriptBuf, 0, sizeof(_scriptBuf));
}

void AppLoader::begin() {
    _refreshApps();
}

void AppLoader::_refreshApps() {
    _appCount    = _sd.listApps(_appNames, MAX_APPS);
    _selectedIdx = 0;
    for (uint8_t i = 0; i < _appCount; i++) {
        _appPtrs[i] = _appNames[i];
    }
    Serial.print("[AppLoader] Found ");
    Serial.print(_appCount);
    Serial.println(" app(s)");
}

void AppLoader::_launchApp(uint8_t idx) {
    if (idx >= _appCount) return;

    // Build full path: /apps/<name>/app.lua
    char path[MAX_APP_NAME + sizeof(APPS_ROOT) + sizeof(APP_SCRIPT) + 3];
    snprintf(path, sizeof(path), "%s/%s/%s",
             APPS_ROOT, _appNames[idx], APP_SCRIPT);

    int n = _sd.readFile(path, _scriptBuf, sizeof(_scriptBuf));
    if (n <= 0) {
        Serial.print("[AppLoader] Failed to read: ");
        Serial.println(path);
        // Show brief error on display
        _ui.clear();
        _ui.drawText(4, 20, "Load error:");
        _ui.drawText(4, 32, _appNames[idx]);
        _ui.render();
        delay(1500);
        return;
    }

    // Spin up a fresh VM — isolates each app completely
    if (!_lua.begin()) {
        Serial.println("[AppLoader] Lua VM init failed");
        return;
    }

    if (!_lua.loadScript(_scriptBuf, (size_t)n)) {
        Serial.println("[AppLoader] Script load/exec failed");
        _lua.end();
        _ui.clear();
        _ui.drawText(4, 20, "Script error.");
        _ui.drawText(4, 32, "Check Serial.");
        _ui.render();
        delay(1500);
        return;
    }

    // Optional init() hook
    _lua.callVoid("init");

    _state = APPSTATE_RUNNING;
    Serial.print("[AppLoader] Running: ");
    Serial.println(_appNames[idx]);
}

void AppLoader::_exitApp() {
    _lua.end();
    _state = APPSTATE_LAUNCHER;
    _refreshApps();
}

void AppLoader::_drawLauncher() {
    _ui.clear();
    if (_appCount == 0) {
        _ui.drawText(4, 16, "No apps found.");
        _ui.drawText(4, 28, "Check SD card.");
        _ui.drawText(4, 44, "MENU to refresh");
    } else {
        _ui.drawMenu(_appPtrs, _appCount, _selectedIdx);
    }
    _ui.drawStatusBar("PigeonOS", "MENU=refresh");
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
        case EVENT_MENU:
            _refreshApps();
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

    } else {
        // Drain event queue — BACK exits, others forwarded to Lua
        while (_input.hasEvent()) {
            InputEvent e = _input.getEvent();

            if (e == EVENT_BACK) {
                _exitApp();
                return;  // State changed; don't call update() this tick
            }

            const char* eStr = _eventToStr(e);
            if (eStr) {
                // Feed into both: polling API (pigeon.getEvent) and callback (on_event)
                _lua.setNextEvent(e);
                _lua.callOnEvent(eStr);
            }
        }

        // Per-tick Lua update — non-blocking
        _lua.callVoid("update");
    }
}

const char* AppLoader::_eventToStr(InputEvent e) {
    switch (e) {
        case EVENT_UP:     return "up";
        case EVENT_DOWN:   return "down";
        case EVENT_LEFT:   return "left";
        case EVENT_RIGHT:  return "right";
        case EVENT_SELECT: return "select";
        case EVENT_BACK:   return "back";
        case EVENT_MENU:   return "menu";
        default:           return nullptr;
    }
}
