#include "LuaEngine.h"
#include "Config.h"

// ─── Module-level pointer for C-callback access ───────────────────────────────
// There is only ever one LuaEngine instance (created in main.ino).
static LuaEngine* s_instance = nullptr;

// ─── pigeon.* C bindings ──────────────────────────────────────────────────────

// pigeon.getEvent() → "up"|"down"|"left"|"right"|"select"|"back"|"menu"|nil
static int l_getEvent(lua_State* L) {
    if (!s_instance) {
        lua_pushnil(L);
        return 1;
    }
    InputEvent e = s_instance->consumeEvent();
    switch (e) {
        case EVENT_UP:     lua_pushstring(L, "up");     break;
        case EVENT_DOWN:   lua_pushstring(L, "down");   break;
        case EVENT_LEFT:   lua_pushstring(L, "left");   break;
        case EVENT_RIGHT:  lua_pushstring(L, "right");  break;
        case EVENT_SELECT: lua_pushstring(L, "select"); break;
        case EVENT_BACK:   lua_pushstring(L, "back");   break;
        case EVENT_MENU:   lua_pushstring(L, "menu");   break;
        default:           lua_pushnil(L);              break;
    }
    return 1;
}

// pigeon.time() → integer milliseconds since boot
static int l_time(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)millis());
    return 1;
}

// pigeon.print(str) → Serial.println
static int l_print(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    Serial.println(s);
    return 0;
}

static const luaL_Reg s_pigeonLib[] = {
    { "getEvent", l_getEvent },
    { "time",     l_time     },
    { "print",    l_print    },
    { nullptr,    nullptr    }
};

// ─── LuaEngine ────────────────────────────────────────────────────────────────

LuaEngine::LuaEngine()
    : _L(nullptr), _loaded(false), _pendingEvent(EVENT_NONE)
{
    s_instance = this;
}

bool LuaEngine::begin() {
    // Destroy any existing state first
    if (_L) {
        lua_close(_L);
        _L = nullptr;
    }
    _loaded       = false;
    _pendingEvent = EVENT_NONE;

    _L = luaL_newstate();
    if (!_L) {
        Serial.println("[Lua] luaL_newstate() failed (out of memory?)");
        return false;
    }

    // Open only safe standard libraries — no io, os, debug, package
    luaL_requiref(_L, "_G",      luaopen_base,   1); lua_pop(_L, 1);
    luaL_requiref(_L, "math",    luaopen_math,   1); lua_pop(_L, 1);
    luaL_requiref(_L, "string",  luaopen_string, 1); lua_pop(_L, 1);
    luaL_requiref(_L, "table",   luaopen_table,  1); lua_pop(_L, 1);

    _registerPigeonLib();
    return true;
}

void LuaEngine::end() {
    if (_L) {
        lua_close(_L);
        _L = nullptr;
    }
    _loaded       = false;
    _pendingEvent = EVENT_NONE;
}

void LuaEngine::_registerPigeonLib() {
    luaL_newlib(_L, s_pigeonLib);   // pushes table
    lua_setglobal(_L, "pigeon");    // pops table, sets global
}

bool LuaEngine::loadScript(const char* src, size_t len) {
    if (!_L || !src || len == 0) return false;

    // Compile
    int err = luaL_loadbuffer(_L, src, len, "@app");
    if (err != LUA_OK) {
        _reportError(_L, "compile");
        return false;
    }

    // Execute the top-level chunk (defines functions, runs module-level code)
    err = lua_pcall(_L, 0, 0, 0);
    if (err != LUA_OK) {
        _reportError(_L, "exec");
        return false;
    }

    _loaded = true;
    return true;
}

bool LuaEngine::callVoid(const char* funcName) {
    if (!_L || !_loaded || !funcName) return false;

    lua_getglobal(_L, funcName);
    if (!lua_isfunction(_L, -1)) {
        lua_pop(_L, 1);
        return false;   // Not an error — optional functions are allowed
    }

    int err = lua_pcall(_L, 0, 0, 0);
    if (err != LUA_OK) {
        _reportError(_L, funcName);
        return false;
    }
    return true;
}

bool LuaEngine::callOnEvent(const char* eventStr) {
    if (!_L || !_loaded || !eventStr) return false;

    lua_getglobal(_L, "on_event");
    if (!lua_isfunction(_L, -1)) {
        lua_pop(_L, 1);
        return false;
    }

    lua_pushstring(_L, eventStr);
    int err = lua_pcall(_L, 1, 0, 0);
    if (err != LUA_OK) {
        _reportError(_L, "on_event");
        return false;
    }
    return true;
}

InputEvent LuaEngine::consumeEvent() {
    InputEvent e  = _pendingEvent;
    _pendingEvent = EVENT_NONE;
    return e;
}

void LuaEngine::_reportError(lua_State* L, const char* context) {
    const char* msg = lua_tostring(L, -1);
    Serial.print("[Lua] ");
    Serial.print(context);
    Serial.print(": ");
    Serial.println(msg ? msg : "(unknown error)");
    lua_pop(L, 1);
}
