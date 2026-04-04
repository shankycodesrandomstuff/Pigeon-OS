#pragma once

#include <stdbool.h>

struct lua_State;

typedef struct {
    struct lua_State *L;
} lua_runtime_t;

bool lua_port_init(lua_runtime_t *rt);
void lua_port_deinit(lua_runtime_t *rt);
bool lua_port_execute_file(lua_runtime_t *rt, const char *path);
bool lua_port_call_global(lua_runtime_t *rt, const char *fn_name, int nargs, int nresults);
