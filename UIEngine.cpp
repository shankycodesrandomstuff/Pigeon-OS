#include "UIEngine.h"
#include <Wire.h>

// Single global driver instance (full-buffer, HW I2C, reset on U8X8_PIN_NONE)
static U8G2_SH1106_128X64_NONAME_F_HW_I2C s_display(U8G2_R0, U8X8_PIN_NONE);

bool UIEngine::begin() {
    Wire.begin(OLED_SDA, OLED_SCL);
    _u8g2  = &s_display;
    _dirty = false;

    if (!_u8g2->begin()) {
        return false;
    }

    _u8g2->setFont(u8g2_font_6x10_tf);
    _u8g2->setFontRefHeightExtendedText();
    _u8g2->setFontPosTop();
    _u8g2->setDrawColor(1);
    _u8g2->clearBuffer();
    return true;
}

void UIEngine::clear() {
    _u8g2->clearBuffer();
    _dirty = true;
}

void UIEngine::render() {
    if (_dirty) {
        _u8g2->sendBuffer();
        _dirty = false;
    }
}

void UIEngine::drawText(uint8_t x, uint8_t y, const char* text) {
    if (!text) return;
    _u8g2->drawStr(x, y, text);
    _dirty = true;
}

void UIEngine::drawMenu(const char* const* items, uint8_t count, uint8_t selectedIndex) {
    if (!items || count == 0) return;

    // Each row is 12px tall; font is 6x10 so text starts 1px down inside row
    const uint8_t itemH   = 12;
    const uint8_t visRows = OLED_HEIGHT / itemH;   // = 5 rows at 64px height
    const uint8_t textX   = 4;

    // Calculate scroll offset so selected item is always visible
    uint8_t scrollTop = 0;
    if (selectedIndex >= visRows) {
        scrollTop = (uint8_t)(selectedIndex - visRows + 1);
    }

    for (uint8_t i = 0; i < visRows; i++) {
        uint8_t idx = scrollTop + i;
        if (idx >= count) break;

        uint8_t rowY = (uint8_t)(i * itemH);

        if (idx == selectedIndex) {
            _u8g2->setDrawColor(1);
            _u8g2->drawBox(0, rowY, OLED_WIDTH, itemH);
            _u8g2->setDrawColor(0);
            _u8g2->drawStr(textX, rowY + 1, items[idx]);
            _u8g2->setDrawColor(1);
        } else {
            _u8g2->drawStr(textX, rowY + 1, items[idx]);
        }
    }

    // Minimal scrollbar on right edge (only when list overflows)
    if (count > visRows) {
        uint8_t barH = (uint8_t)((uint16_t)visRows * OLED_HEIGHT / count);
        if (barH < 3) barH = 3;   // minimum thumb size
        uint8_t barY = (uint8_t)((uint16_t)scrollTop * OLED_HEIGHT / count);

        _u8g2->drawFrame(OLED_WIDTH - 3, 0, 3, OLED_HEIGHT);
        _u8g2->drawBox(OLED_WIDTH - 3, barY, 3, barH);
    }

    _dirty = true;
}

void UIEngine::drawStatusBar(const char* left, const char* right) {
    // 10px bar at the very bottom of the screen
    const uint8_t barY = OLED_HEIGHT - 10;
    _u8g2->drawHLine(0, barY, OLED_WIDTH);

    if (left) {
        _u8g2->drawStr(2, barY + 1, left);
    }
    if (right) {
        // Right-align: measure string width first
        uint16_t w = _u8g2->getStrWidth(right);
        if (w < OLED_WIDTH - 4) {
            _u8g2->drawStr((uint8_t)(OLED_WIDTH - w - 2), barY + 1, right);
        }
    }
    _dirty = true;
}
