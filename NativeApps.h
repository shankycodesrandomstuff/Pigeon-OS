#pragma once
#include <Arduino.h>
#include "App.h"
#include "InputManager.h"
#include "UIEngine.h"
#include "SDManager.h"

class ButtonEchoApp : public App {
public:
    ButtonEchoApp(InputManager& input, UIEngine& ui);
    void setup() override;
    void loop() override;

private:
    InputManager& _input;
    UIEngine& _ui;
    char _lastEvent[20];
};

class SDStatusApp : public App {
public:
    SDStatusApp(InputManager& input, UIEngine& ui, SDManager& sd);
    void setup() override;
    void loop() override;

private:
    InputManager& _input;
    UIEngine& _ui;
    SDManager& _sd;
    uint16_t _eventCount;
};
