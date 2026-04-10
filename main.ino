// ─────────────────────────────────────────────────────────────────────────────
//  PigeonOS — main.ino  (Kernel)
//  Target : ESP32 DEVKIT v1
//  Framework: Arduino
// ─────────────────────────────────────────────────────────────────────────────
#include "Config.h"
#include "InputManager.h"
#include "UIEngine.h"
#include "SDManager.h"
#include "LuaEngine.h"
#include "AppLoader.h"

// ─── Module singletons ────────────────────────────────────────────────────────
static InputManager inputMgr;
static UIEngine     uiEngine;
static SDManager    sdMgr;
static LuaEngine    luaEngine;                                      // owns Lua VM
static AppLoader    appLoader(inputMgr, uiEngine, sdMgr, luaEngine);

// ─── Kernel timing ────────────────────────────────────────────────────────────
static uint32_t s_lastTick = 0;

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {}   // Wait up to 2s on USB-serial
    Serial.println("\n[PigeonOS] Booting...");

    pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(BUILTIN_LED, LOW);

    // ── Display ───────────────────────────────────────────────────────────────
    if (!uiEngine.begin()) {
        Serial.println("[UIEngine] OLED init FAILED");
        // Continue without display — Serial still works
    } else {
        uiEngine.clear();
        uiEngine.drawText(20, 18, "PigeonOS");
        uiEngine.drawText(16, 32, "v0.1  booting");
        uiEngine.render();
    }

    // ── SD Card ───────────────────────────────────────────────────────────────
    if (!sdMgr.begin()) {
        Serial.println("[SDManager] SD init FAILED");
        uiEngine.clear();
        uiEngine.drawText(4, 18, "SD card error!");
        uiEngine.drawText(4, 30, "Check wiring.");
        uiEngine.render();
        delay(2000);
        // Non-fatal — launcher will show "No apps found"
    }

    // ── Input ─────────────────────────────────────────────────────────────────
    inputMgr.begin();
    Serial.println("[InputManager] ready");

    // ── AppLoader ─────────────────────────────────────────────────────────────
    appLoader.begin();
    Serial.println("[AppLoader] ready");

    s_lastTick = millis();
    Serial.println("[PigeonOS] Boot complete\n");
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    // Enforce fixed ~10 ms kernel tick without blocking
    if ((now - s_lastTick) < (uint32_t)KERNEL_TICK_MS) {
        return;
    }
    s_lastTick = now;

    // ── Kernel cycle (order matters) ──────────────────────────────────────────
    inputMgr.poll();     // 1. Collect & debounce button state
    appLoader.update();  // 2. Run launcher or active Lua app
    uiEngine.render();   // 3. Flush framebuffer if dirty (no-op otherwise)
}
