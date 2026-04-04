#include "lua_port.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "hal_lua_api.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

static const char *TAG = "lua_port";

static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;

    if (nsize == 0) {
        heap_caps_free(ptr);
        return NULL;
    }

    return heap_caps_realloc(ptr, nsize, MALLOC_CAP_8BIT);
}

bool lua_port_init(lua_runtime_t *rt)
{
    if (!rt) {
        return false;
    }

    rt->L = lua_newstate(lua_alloc, NULL);
    if (!rt->L) {
        ESP_LOGE(TAG, "failed to create Lua state");
        return false;
    }

    luaL_openlibs(rt->L);
    hal_lua_register(rt->L);
    return true;
}

void lua_port_deinit(lua_runtime_t *rt)
{
    if (rt && rt->L) {
        lua_close(rt->L);
        rt->L = NULL;
    }
}

bool lua_port_execute_file(lua_runtime_t *rt, const char *path)
{
    if (!rt || !rt->L || !path) {
        return false;
    }

    int rc = luaL_loadfile(rt->L, path);
    if (rc != LUA_OK) {
        ESP_LOGE(TAG, "loadfile failed: %s", lua_tostring(rt->L, -1));
        lua_pop(rt->L, 1);
        return false;
    }

    rc = lua_pcall(rt->L, 0, LUA_MULTRET, 0);
    if (rc != LUA_OK) {
        ESP_LOGE(TAG, "pcall failed: %s", lua_tostring(rt->L, -1));
        lua_pop(rt->L, 1);
        return false;
    }

    return true;
}

bool lua_port_call_global(lua_runtime_t *rt, const char *fn_name, int nargs, int nresults)
{
    if (!rt || !rt->L || !fn_name) {
        return false;
    }

    lua_getglobal(rt->L, fn_name);
    if (!lua_isfunction(rt->L, -1)) {
        lua_pop(rt->L, 1);
        if (nargs > 0) {
            lua_pop(rt->L, nargs);
        }
        return false;
    }

    if (nargs > 0) {
        lua_insert(rt->L, -1 - nargs);
    }

    int rc = lua_pcall(rt->L, nargs, nresults, 0);
    if (rc != LUA_OK) {
        ESP_LOGE(TAG, "error in %s: %s", fn_name, lua_tostring(rt->L, -1));
        lua_pop(rt->L, 1);
        return false;
    }

    return true;
}
