#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "Config.h"

class UIEngine {
public:
    bool begin();
    void clear();
    void render();

    void drawText(uint8_t x, uint8_t y, const char* text);
    void drawMenu(const char* const* items, uint8_t count, uint8_t selectedIndex);
    void drawStatusBar(const char* left, const char* right);

private:
    // Full-buffer driver — instantiated in UIEngine.cpp, pointer stored here
    // so callers only need this header, not U8g2lib.h
    U8G2_SH1106_128X64_NONAME_F_HW_I2C* _u8g2;
    bool _dirty;
};
