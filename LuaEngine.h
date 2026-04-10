#pragma once
#include <Arduino.h>
#include "InputManager.h"

// Lua 5.3/5.4 headers must be compiled as C
extern "C" {
#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
}

class LuaEngine {
public:
    LuaEngine();

    // (Re-)create a fresh Lua VM. Call before loadScript().
    bool begin();

    // Destroy the Lua VM and free all memory.
    void end();

    // Load and execute Lua source from an in-memory buffer.
    // Returns true on success; prints error to Serial on failure.
    bool loadScript(const char* src, size_t len);

    // Call a zero-argument Lua global function by name.
    // Returns false silently if the function does not exist.
    bool callVoid(const char* funcName);

    // Call on_event(eventStr) in Lua.
    bool callOnEvent(const char* eventStr);

    // True after a successful loadScript().
    bool isLoaded() const { return _loaded; }

    // Feed an event so pigeon.getEvent() can return it inside Lua.
    void       setNextEvent(InputEvent e) { _pendingEvent = e; }
    InputEvent consumeEvent();

private:
    lua_State*  _L;
    bool        _loaded;
    InputEvent  _pendingEvent;

    void _registerPigeonLib();

    // Print top-of-stack Lua error to Serial, then pop it.
    static void _reportError(lua_State* L, const char* context);
};
