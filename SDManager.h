#pragma once
#include <Arduino.h>

class SDManager {
public:
    SDManager();
    bool begin();
    bool isReady() const { return _ready; }

private:
    bool _ready;
};
