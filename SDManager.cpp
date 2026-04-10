#include "SDManager.h"
#include <SPI.h>
#include <SD.h>

bool SDManager::begin() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    _ready = SD.begin(SD_CS);
    if (!_ready) {
        Serial.println("[SDManager] SD.begin() failed");
    }
    return _ready;
}

uint8_t SDManager::listApps(char outNames[][MAX_APP_NAME], uint8_t maxCount) {
    if (!_ready || !outNames || maxCount == 0) return 0;

    File root = SD.open(APPS_ROOT);
    if (!root) {
        Serial.println("[SDManager] Cannot open " APPS_ROOT);
        return 0;
    }
    if (!root.isDirectory()) {
        root.close();
        Serial.println("[SDManager] " APPS_ROOT " is not a directory");
        return 0;
    }

    uint8_t count = 0;
    while (count < maxCount) {
        File entry = root.openNextFile();
        if (!entry) break;

        if (entry.isDirectory()) {
            // Verify app.lua exists
            char scriptPath[64];
            snprintf(scriptPath, sizeof(scriptPath), "%s/%s/%s",
                     APPS_ROOT, entry.name(), APP_SCRIPT);

            File check = SD.open(scriptPath);
            if (check) {
                check.close();
                strncpy(outNames[count], entry.name(), MAX_APP_NAME - 1);
                outNames[count][MAX_APP_NAME - 1] = '\0';
                count++;
            }
        }
        entry.close();
    }
    root.close();
    return count;
}

int SDManager::readFile(const char* path, char* outBuf, size_t bufSize) {
    if (!_ready || !path || !outBuf || bufSize == 0) return -1;

    File f = SD.open(path);
    if (!f) {
        Serial.print("[SDManager] Cannot open: ");
        Serial.println(path);
        return -1;
    }

    size_t total = 0;
    while (f.available() && total < bufSize - 1) {
        int c = f.read();
        if (c < 0) break;
        outBuf[total++] = (char)c;
    }
    outBuf[total] = '\0';
    f.close();
    return (int)total;
}
