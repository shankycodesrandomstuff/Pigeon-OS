#pragma once
#include <Arduino.h>
#include "Config.h"
#include "App.h"
#include "InputManager.h"
#include "UIEngine.h"
#include "SDManager.h"

enum AppState : uint8_t {
    APPSTATE_LAUNCHER = 0,
    APPSTATE_RUNNING
};

class AppLoader {
public:
    AppLoader(InputManager& input, UIEngine& ui, SDManager& sd);

    void begin();
    void update();

private:
    InputManager& _input;
    UIEngine& _ui;
    SDManager& _sd;

    AppState _state;
    App* _apps[MAX_APPS];
    const char* _appNames[MAX_APPS];
    uint8_t _appCount;
    uint8_t _selectedIdx;
    int8_t _activeAppIdx;

    void _initRegistry();
    void _launchApp(uint8_t idx);
    void _exitApp();
    void _drawLauncher();
    void _handleLauncherEvent(InputEvent e);
};
