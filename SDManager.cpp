#include "SDManager.h"
#include "Config.h"
#include <SPI.h>
#include <SD.h>

SDManager::SDManager()
    : _ready(false)
{}

bool SDManager::begin() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    _ready = SD.begin(SD_CS);
    if (!_ready) {
        Serial.println("[SDManager] SD.begin() failed");
    }
    return _ready;
}
