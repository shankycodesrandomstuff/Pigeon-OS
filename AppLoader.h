#pragma once
#include <Arduino.h>
#include "Config.h"
#include "InputManager.h"
#include "UIEngine.h"
#include "SDManager.h"
#include "LuaEngine.h"

enum AppState : uint8_t {
    APPSTATE_LAUNCHER = 0,
    APPSTATE_RUNNING
};

class AppLoader {
public:
    AppLoader(InputManager& input, UIEngine& ui,
              SDManager& sd,    LuaEngine& lua);

    // Call once after all modules are initialised.
    void begin();

    // Call every kernel tick. Handles launcher and running-app logic.
    void update();

private:
    InputManager& _input;
    UIEngine&     _ui;
    SDManager&    _sd;
    LuaEngine&    _lua;

    AppState _state;

    // App name storage + pointer array for drawMenu()
    char        _appNames[MAX_APPS][MAX_APP_NAME];
    const char* _appPtrs[MAX_APPS];
    uint8_t     _appCount;
    uint8_t     _selectedIdx;

    // Lua script read buffer (lives here to avoid stack allocation)
    char _scriptBuf[MAX_FILE_SIZE];

    void _refreshApps();
    void _launchApp(uint8_t idx);
    void _exitApp();
    void _drawLauncher();
    void _handleLauncherEvent(InputEvent e);

    // Convert InputEvent → C-string; returns nullptr for EVENT_NONE.
    static const char* _eventToStr(InputEvent e);
};
