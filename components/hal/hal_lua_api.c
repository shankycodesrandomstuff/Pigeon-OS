#include "hal_lua_api.h"

#include "hal.h"
#include "lua.h"
#include "lauxlib.h"

static int l_gpio_write(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);
    int value = luaL_checkinteger(L, 2);

    if (pin < 0 || pin > 39) {
        return luaL_error(L, "invalid GPIO pin");
    }

    hal_gpio_write((uint8_t)pin, value != 0);
    return 0;
}

static int l_gpio_read(lua_State *L)
{
    int pin = luaL_checkinteger(L, 1);

    if (pin < 0 || pin > 39) {
        return luaL_error(L, "invalid GPIO pin");
    }

    lua_pushinteger(L, hal_gpio_read((uint8_t)pin) ? 1 : 0);
    return 1;
}

static int l_delay(lua_State *L)
{
    int ms = luaL_checkinteger(L, 1);

    if (ms < 0) {
        return luaL_error(L, "delay must be >= 0");
    }

    hal_delay_ms((uint32_t)ms);
    return 0;
}

void hal_lua_register(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_gpio_write);
    lua_setfield(L, -2, "write");
    lua_pushcfunction(L, l_gpio_read);
    lua_setfield(L, -2, "read");
    lua_setglobal(L, "gpio");

    lua_pushcfunction(L, l_delay);
    lua_setglobal(L, "delay");
}
