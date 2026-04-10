#pragma once
#include <Arduino.h>
#include "Config.h"

class SDManager {
public:
    bool begin();

    // Scan /apps/ directory and populate outNames[][MAX_APP_NAME].
    // Only entries that contain APP_SCRIPT are included.
    // Returns number of apps found (0..maxCount).
    uint8_t listApps(char outNames[][MAX_APP_NAME], uint8_t maxCount);

    // Read file at path into outBuf (null-terminated, max bufSize-1 bytes).
    // Returns bytes read on success, -1 on error.
    int readFile(const char* path, char* outBuf, size_t bufSize);

    bool isReady() const { return _ready; }

private:
    bool _ready;
};
