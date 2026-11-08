/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Attach Lua devices and their terminals to simulated SPI and I2C buses.
 * RU: Подключает Lua-устройства и их терминалы к шинам SPI и I2C симулятора. */
#ifndef GEANY_LUA_BUS_H
#define GEANY_LUA_BUS_H

#include "geany-lua-device.h"
#include <stdlib.h>
#include <string.h>

typedef struct LuaBus {
    LuaDevice lua;
    struct LuaBus *terminal_next;
    char input_path[64];
    vpiHandle pin[6];
    unsigned char queue[LUA_DEVICE_LIMIT], frame[LUA_DEVICE_LIMIT];
    unsigned head, queued, length, bits, input, output, phase, next, address;
    int clock, data, drive, selected, writing, override, sending, reset;
} LuaBus;

static LuaBus *lua_terminals;

/* EN: Deliver queued terminal commands to running Lua devices. unused is a VPI argument.
 * RU: Доставляет ожидающие команды запущенным Lua-устройствам. unused — аргумент VPI. */
static PLI_INT32 lua_bus_console_poll(PLI_BYTE8 *unused)
{
    (void)unused;
    for (LuaBus *bus = lua_terminals; bus; bus = bus->terminal_next)
    {
        FILE *input = fopen(bus->input_path, "rb");
        if (!input)
            continue;

        char line[LUA_DEVICE_LIMIT + 1];
        size_t length = fread(line, 1, sizeof line, input);
        int valid = !ferror(input) && length <= LUA_DEVICE_LIMIT;
        fclose(input);
        if (remove(bus->input_path) != 0)
            continue;
        if (valid)
        {
            line[length] = 0;
            lua_device_command(&bus->lua, line);
        }
    }
    return 0;
}

static int lua_bus_call(LuaBus *bus, enum LuaDeviceCallback callback, int input, int index,
                        const unsigned char *frame, unsigned length, int enqueue)
{
    if (bus->lua.failed || !(bus->lua.enabled & (1u << callback)))
        return -1;
    lua_device_call(&bus->lua, callback, input, index, frame, length);
    if (enqueue && !bus->lua.failed) {
        if (bus->lua.count > LUA_DEVICE_LIMIT - bus->queued)
            lua_device_error(&bus->lua, "device transmit queue overflow");
        else
            for (unsigned i = 0; i < bus->lua.count; i++) {
                bus->queue[(bus->head + bus->queued) % LUA_DEVICE_LIMIT] = bus->lua.output[i];
                bus->queued++;
            }
    }
    return bus->lua.count ? bus->lua.output[0] : -1;
}

static unsigned lua_bus_pop(LuaBus *bus)
{
    if (!bus->queued)
        return 255;
    unsigned byte = bus->queue[bus->head];
    bus->head = (bus->head + 1) % LUA_DEVICE_LIMIT;
    bus->queued--;
    return byte;
}

static void lua_bus_append(LuaBus *bus, unsigned byte)
{
    if (bus->length == LUA_DEVICE_LIMIT)
        lua_device_error(&bus->lua, "device receive frame overflow");
    else
        bus->frame[bus->length++] = byte;
}

/* EN: Stop the Lua worker and release the bus from callback.
 * RU: Останавливает поток Lua и освобождает шину из callback. */
static PLI_INT32 lua_bus_free(p_cb_data callback)
{
    LuaBus *bus = (LuaBus *)callback->user_data;
    LuaBus **entry = &lua_terminals;
    while (*entry && *entry != bus)
        entry = &(*entry)->terminal_next;
    if (*entry)
        *entry = bus->terminal_next;
    lua_device_close(&bus->lua);
    if (bus->lua.terminal)
        fclose(bus->lua.terminal);
    free(bus);
    return 0;
}

/* EN: Bind call pins and start a Lua worker for SPI or i2c. Return NULL on failure.
 * RU: Подключает выводы call и запускает Lua для SPI или i2c. При ошибке возвращает NULL. */
static LuaBus *lua_bus_open(vpiHandle call, int i2c)
{
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    vpiHandle pins[6];
    for (unsigned i = 0; i < 5; i++) {
        pins[i] = vpi_scan(args);
        if (!pins[i])
            return NULL;
    }
    pins[5] = vpi_scan(args);
    if (pins[5] && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    for (unsigned i = 0; i < 4; i++)
        if ((!i2c || i != 2) && vpi_get(vpiSize, pins[i]) != 1)
            return NULL;
    if (pins[5] && vpi_get(vpiSize, pins[5]) != 1)
        return NULL;
    s_vpi_vecval target = i2c ? vector_word(pins[2]) : (s_vpi_vecval){0};
    if (i2c && (target.bval || target.aval < 8 || target.aval > 119))
        return NULL;
    LuaBus *bus = calloc(1, sizeof *bus);
    if (!bus)
        return NULL;
    memcpy(bus->pin, pins, sizeof pins);
    bus->clock = bus->data = i2c;
    bus->drive = vpiZ;
    bus->override = -1;
    bus->reset = -1;
    bus->address = target.aval;
    s_vpi_value path = {.format = vpiStringVal};
    vpi_get_value(pins[4], &path);
    if (getenv("GEANY_LUA_TERMINAL"))
    {
        static unsigned next_id;
        unsigned id = ++next_id;
        const char *kind = i2c ? "i2c" : "spi";
        char output_path[64];
        snprintf(output_path, sizeof output_path, "geany-lua-%s-%u.log", kind, id);
        snprintf(bus->input_path, sizeof bus->input_path, "geany-lua-%s-%u.in", kind, id);
        bus->lua.terminal = fopen(output_path, "wb");
    }
    if (!path.value.str || !lua_device_start(&bus->lua, path.value.str)) {
        if (bus->lua.terminal)
            fclose(bus->lua.terminal);
        free(bus);
        return NULL;
    }
    s_cb_data cleanup = {.reason = cbEndOfSimulation, .cb_rtn = lua_bus_free, .user_data = (PLI_BYTE8 *)bus};
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback) {
        lua_bus_free(&cleanup);
        return NULL;
    }
    vpi_free_object(callback);
    vpi_put_userdata(call, bus);
    if (bus->lua.terminal)
    {
        bus->terminal_next = lua_terminals;
        lua_terminals = bus;
    }
    lua_bus_call(bus, LUA_RESET, -1, -1, NULL, 0, 1);
    return bus;
}

static int lua_spi_edge(LuaBus *bus)
{
    int clock = scalar(bus->pin[0]);
    int selected = scalar(bus->pin[1]) == vpi0;
    int drive = bus->drive;
    if (!selected) {
        if (bus->selected)
            lua_bus_call(bus, LUA_FINISH, -1, -1, bus->frame, bus->length, 1);
        bus->length = bus->bits = bus->input = 0;
        bus->override = -1;
        drive = vpiZ;
    } else {
        if (!bus->selected) {
            bus->sending = bus->queued != 0;
            bus->output = bus->sending ? bus->queue[bus->head] : 255;
            drive = bus->output & 128 ? vpi1 : vpi0;
        }
        if (bus->clock == 0 && clock == vpi1) {
            unsigned bit = scalar(bus->pin[2]) == vpi1;
            bus->override = lua_bus_call(bus, LUA_SHIFT, bit, bus->bits, NULL, 0, 0);
            if (bus->override > 1 || ((bus->lua.enabled & (1u << LUA_SHIFT)) && bus->lua.count > 1))
                lua_device_error(&bus->lua, "SPI shift must return nil, 0 or 1");
            bus->input = (bus->input << 1) | bit;
            if (++bus->bits == 8) {
                if (bus->sending)
                    lua_bus_pop(bus);
                if (bus->lua.enabled & (1u << LUA_FINISH))
                    lua_bus_append(bus, bus->input);
                lua_bus_call(bus, LUA_RECEIVE, bus->input, -1, NULL, 0, 1);
                bus->bits = bus->input = 0;
                bus->sending = bus->queued != 0;
                bus->output = bus->sending ? bus->queue[bus->head] : 255;
            }
        } else if (bus->clock == 1 && clock == vpi0) {
            int bit = bus->override < 0 ? (int)((bus->output >> (7 - bus->bits)) & 1) : bus->override;
            drive = bit ? vpi1 : vpi0;
            bus->override = -1;
        }
    }
    bus->selected = selected;
    bus->clock = clock == vpi1;
    return drive;
}

static void lua_i2c_read(LuaBus *bus)
{
    if (!bus->queued)
        lua_bus_call(bus, LUA_READ, -1, -1, NULL, 0, 1);
    bus->output = lua_bus_pop(bus);
}

static int lua_i2c_edge(LuaBus *bus)
{
    int scl = scalar(bus->pin[0]), sda = scalar(bus->pin[1]);
    if ((scl != vpi0 && scl != vpi1) || (sda != vpi0 && sda != vpi1))
        return bus->drive;
    int clock = scl == vpi1, data = sda == vpi1;
    int drive = bus->drive;
    if (clock && bus->clock && data != bus->data) {
        if (bus->writing)
            lua_bus_call(bus, LUA_RECEIVE, -1, -1, bus->frame, bus->length, 1);
        bus->writing = bus->selected = 0;
        bus->length = bus->bits = bus->input = 0;
        bus->phase = data ? I2C_IDLE : I2C_RECEIVE;
        drive = vpiZ;
    } else if (clock && !bus->clock) {
        switch (bus->phase) {
        case I2C_RECEIVE:
            bus->input = (bus->input << 1) | data;
            if (++bus->bits == 8) {
                if (!bus->selected) {
                    int accept = lua_bus_call(bus, LUA_ADDRESS, bus->input, -1, NULL, 0, 0);
                    bus->selected = accept < 0 ? (bus->input >> 1) == bus->address : accept;
                    bus->writing = bus->selected && !(bus->input & 1);
                } else {
                    if (bus->lua.enabled & (1u << LUA_RECEIVE))
                        lua_bus_append(bus, bus->input);
                    lua_bus_call(bus, LUA_WRITE, bus->input, -1, NULL, 0, 1);
                }
                bus->next = !bus->selected ? I2C_IDLE : bus->writing ? I2C_RECEIVE : I2C_SEND;
                bus->phase = I2C_ACK;
                bus->bits = bus->input = 0;
            }
            break;
        case I2C_SEND:
            if (++bus->bits == 8) {
                bus->phase = I2C_MASTER_ACK;
                bus->bits = 0;
            }
            break;
        case I2C_MASTER_ACK:
            bus->next = data ? I2C_IDLE : I2C_SEND;
            bus->bits = 1;
            break;
        case I2C_ACK:
            bus->bits = 1;
            break;
        }
    } else if (!clock && bus->clock) {
        if ((bus->phase == I2C_ACK || bus->phase == I2C_MASTER_ACK) && bus->bits) {
            bus->phase = bus->next;
            bus->bits = 0;
            if (bus->phase == I2C_SEND)
                lua_i2c_read(bus);
        }
        drive = vpiZ;
        if ((bus->phase == I2C_ACK && bus->next != I2C_IDLE) ||
            (bus->phase == I2C_SEND && !(bus->output & (128u >> bus->bits))))
            drive = vpi0;
    }
    bus->clock = clock;
    bus->data = data;
    return drive;
}

static PLI_INT32 lua_bus_update(PLI_BYTE8 *i2c)
{
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    LuaBus *bus = vpi_get_userdata(call);
    if (!bus)
        bus = lua_bus_open(call, i2c != NULL);
    int drive = vpiZ;
    if (bus && bus->pin[5] && scalar(bus->pin[5]) == vpi0) {
        if (bus->reset == 0) {
            bus->head = bus->queued = bus->length = bus->bits = bus->input = 0;
            bus->phase = bus->selected = bus->writing = bus->sending = 0;
            bus->override = -1;
            lua_bus_call(bus, LUA_RESET, -1, -1, NULL, 0, 1);
        }
        bus->reset = 1;
        bus->clock = scalar(bus->pin[0]) == vpi1;
        bus->data = scalar(bus->pin[1]) == vpi1;
    } else if (bus) {
        bus->reset = 0;
        drive = i2c ? lua_i2c_edge(bus) : lua_spi_edge(bus);
    }
    if (!bus || bus->lua.failed) {
        vpi_printf("Geany Lua device: %s\n", bus ? bus->lua.error : "invalid device configuration");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
    } else if (drive != bus->drive) {
        s_vpi_value value = {.format = vpiScalarVal};
        value.value.scalar = drive;
        vpi_put_value(bus->pin[3], &value, NULL, vpiNoDelay);
        bus->drive = drive;
    }
    return 0;
}
#endif
