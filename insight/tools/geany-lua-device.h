/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_LUA_DEVICE_H
#define GEANY_LUA_DEVICE_H

#include <pthread.h>
#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

enum { LUA_DEVICE_LIMIT = 4096 };
enum LuaDeviceCallback { LUA_RESET, LUA_SHIFT, LUA_RECEIVE, LUA_FINISH, LUA_READ, LUA_WRITE };
static const char *const lua_device_callbacks[] = {"reset", "shift", "receive", "finish", "read", "write"};

typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    const char *path;
    enum LuaDeviceCallback callback;
    unsigned enabled;
    int references[sizeof lua_device_callbacks / sizeof *lua_device_callbacks];
    int pending, closing, failed, input, index;
    const unsigned char *frame;
    unsigned frame_length, count;
    unsigned char output[LUA_DEVICE_LIMIT];
    char error[512];
} LuaDevice;

static void lua_device_budget(lua_State *lua, lua_Debug *debug)
{
    (void)debug;
    luaL_error(lua, "device instruction budget exhausted");
}

static void lua_device_error(LuaDevice *device, const char *error)
{
    device->failed = 1;
    snprintf(device->error, sizeof device->error, "%s", error ? error : "Lua error");
}

static void *lua_device_worker(void *data)
{
    LuaDevice *device = data;
    lua_State *lua = luaL_newstate();
    pthread_mutex_lock(&device->mutex);
    if (!lua)
        lua_device_error(device, "cannot create Lua state");
    else {
        static const luaL_Reg libraries[] = {
            {"_G", luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
            {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
            {LUA_UTF8LIBNAME, luaopen_utf8}, {NULL, NULL}
        };
        for (const luaL_Reg *lib = libraries; lib->name; lib++) {
            luaL_requiref(lua, lib->name, lib->func, 1);
            lua_pop(lua, 1);
        }
        lua_pushnil(lua); lua_setglobal(lua, "dofile");
        lua_pushnil(lua); lua_setglobal(lua, "loadfile");
        lua_sethook(lua, lua_device_budget, LUA_MASKCOUNT, 1000000);
        if (luaL_loadfilex(lua, device->path, "t") || lua_pcall(lua, 0, 0, 0))
            lua_device_error(device, lua_tostring(lua, -1));
        else for (unsigned i = 0; i < sizeof lua_device_callbacks / sizeof *lua_device_callbacks; i++) {
            lua_pushglobaltable(lua);
            lua_pushstring(lua, lua_device_callbacks[i]);
            lua_rawget(lua, -2);
            if (lua_isfunction(lua, -1)) {
                device->enabled |= 1u << i;
                device->references[i] = luaL_ref(lua, LUA_REGISTRYINDEX);
            } else {
                if (!lua_isnil(lua, -1))
                    lua_device_error(device, "device callback must be a function");
                lua_pop(lua, 1);
            }
            lua_pop(lua, 1);
        }
    }
    for (;;) {
        device->pending = 0;
        pthread_cond_signal(&device->ready);
        while (!device->pending && !device->closing)
            pthread_cond_wait(&device->ready, &device->mutex);
        if (device->closing)
            break;
        device->count = 0;
        if (device->failed)
            continue;
        if (!(device->enabled & (1u << device->callback)))
            continue;
        lua_rawgeti(lua, LUA_REGISTRYINDEX, device->references[device->callback]);
        if (!lua_isnil(lua, -1)) {
            int arguments = 0;
            if (device->frame) {
                lua_createtable(lua, device->frame_length, 0);
                for (unsigned i = 0; i < device->frame_length; i++) {
                    lua_pushinteger(lua, device->frame[i]);
                    lua_rawseti(lua, -2, i + 1);
                }
                arguments = 1;
            } else if (device->input >= 0) {
                lua_pushinteger(lua, device->input);
                arguments = 1;
                if (device->index >= 0) {
                    lua_pushinteger(lua, device->index);
                    arguments++;
                }
            }
            lua_sethook(lua, lua_device_budget, LUA_MASKCOUNT, 1000000);
            if (lua_pcall(lua, arguments, LUA_MULTRET, 0))
                lua_device_error(device, lua_tostring(lua, -1));
            else for (int i = 1; i <= lua_gettop(lua); i++) {
                if (lua_isnil(lua, i))
                    continue;
                int valid;
                lua_Integer byte = lua_tointegerx(lua, i, &valid);
                if (lua_type(lua, i) != LUA_TNUMBER || !valid || byte < 0 || byte > 255 || device->count == LUA_DEVICE_LIMIT) {
                    lua_device_error(device, "callback must return nil or up to 4096 bytes (0–255)");
                    break;
                }
                device->output[device->count++] = (unsigned char)byte;
            }
        }
        lua_settop(lua, 0);
    }
    pthread_mutex_unlock(&device->mutex);
    if (lua)
        lua_close(lua);
    return NULL;
}

static int lua_device_start(LuaDevice *device, const char *path)
{
    device->path = path;
    device->pending = 1;
    if (pthread_mutex_init(&device->mutex, NULL))
        return 0;
    if (pthread_cond_init(&device->ready, NULL)) {
        pthread_mutex_destroy(&device->mutex);
        return 0;
    }
    if (pthread_create(&device->thread, NULL, lua_device_worker, device)) {
        pthread_cond_destroy(&device->ready);
        pthread_mutex_destroy(&device->mutex);
        return 0;
    }
    pthread_mutex_lock(&device->mutex);
    while (device->pending)
        pthread_cond_wait(&device->ready, &device->mutex);
    pthread_mutex_unlock(&device->mutex);
    return 1;
}

static void lua_device_call(LuaDevice *device, enum LuaDeviceCallback callback, int input, int index,
                            const unsigned char *frame, unsigned length)
{
    pthread_mutex_lock(&device->mutex);
    device->callback = callback;
    device->input = input;
    device->index = index;
    device->frame = frame;
    device->frame_length = length;
    device->pending = 1;
    pthread_cond_signal(&device->ready);
    while (device->pending)
        pthread_cond_wait(&device->ready, &device->mutex);
    pthread_mutex_unlock(&device->mutex);
}

static void lua_device_close(LuaDevice *device)
{
    pthread_mutex_lock(&device->mutex);
    device->closing = 1;
    pthread_cond_signal(&device->ready);
    pthread_mutex_unlock(&device->mutex);
    pthread_join(device->thread, NULL);
    pthread_cond_destroy(&device->ready);
    pthread_mutex_destroy(&device->mutex);
}
#endif
