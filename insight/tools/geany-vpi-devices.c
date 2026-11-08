/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Registers the C test devices exposed to the Verilog harness.
 * RU: Регистрирует тестовые устройства C, доступные тестбенчу Verilog. */
#include <vpi_user.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "geany-ddr4.h"
#include "geany-spi-memory.h"
#include "geany-i2c-eeprom.h"

enum { ADDRESS, DATA, CHIP_N, WRITE_N, OUTPUT_N, DRIVE, PORT_COUNT };

typedef struct {
    s_vpi_vecval words[2];
    int written;
} SramCell;

typedef struct {
    vpiHandle ports[PORT_COUNT];
    SramCell *cells;
    unsigned words, address_mask;
    s_vpi_vecval driven[2];
    int drive_valid;
} Sram;

typedef int (*ImageWrite)(void *, uint32_t, uint64_t);

static int image_load(vpiHandle image, ImageWrite write, void *device)
{
    if (!image)
        return 1;
    s_vpi_value value = {0};
    value.format = vpiStringVal;
    vpi_get_value(image, &value);
    FILE *file = fopen(value.value.str, "r");
    if (!file) {
        vpi_printf("Geany memory: cannot open preload image.\n");
        return 0;
    }
    char token[64];
    uint64_t address = 0;
    int valid = 1;
    while (fscanf(file, "%63s", token) == 1) {
        if (token[0] == '#') {
            int ch;
            while ((ch = fgetc(file)) != EOF && ch != '\n')
                ;
            continue;
        }
        const char *digits = token + (token[0] == '@');
        char *end;
        errno = 0;
        uint64_t number = strtoull(digits, &end, 16);
        if (errno || end == digits || *end || *digits == '-' || strlen(token) == 63) {
            valid = 0;
            break;
        }
        if (token[0] == '@')
            address = number;
        else if (address > UINT32_MAX || !write(device, (uint32_t)address++, number)) {
            valid = 0;
            break;
        }
    }
    if (ferror(file))
        valid = 0;
    fclose(file);
    if (!valid)
        vpi_printf("Geany memory: invalid hex image or address/value outside the model.\n");
    return valid;
}

static int sram_image_write(void *data, uint32_t address, uint64_t value)
{
    Sram *sram = data;
    unsigned bits = (unsigned)vpi_get(vpiSize, sram->ports[DATA]);
    if (address > sram->address_mask || (bits < 64 && (value >> bits)))
        return 0;
    SramCell *cell = &sram->cells[address];
    cell->words[0].aval = (PLI_INT32)value;
    cell->words[1].aval = (PLI_INT32)(value >> 32);
    cell->written = 1;
    return 1;
}

static PLI_INT32 sram_free(p_cb_data callback)
{
    Sram *sram = (Sram *)callback->user_data;
    free(sram->cells);
    free(sram);
    return 0;
}

static Sram *sram_open(vpiHandle call)
{
    vpiHandle ports[PORT_COUNT];
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    for (unsigned i = 0; i < PORT_COUNT; i++) {
        ports[i] = vpi_scan(args);
        if (!ports[i])
            return NULL;
    }
    vpiHandle image = vpi_scan(args);
    if (image && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    int address_bits = vpi_get(vpiSize, ports[ADDRESS]);
    int data_bits = vpi_get(vpiSize, ports[DATA]);
    if (address_bits < 1 || address_bits > 16 || data_bits < 1 || data_bits > 64 ||
        vpi_get(vpiSize, ports[DRIVE]) != data_bits ||
        vpi_get(vpiSize, ports[CHIP_N]) != 1 || vpi_get(vpiSize, ports[WRITE_N]) != 1 ||
        vpi_get(vpiSize, ports[OUTPUT_N]) != 1)
        return NULL;

    Sram *sram = calloc(1, sizeof *sram);
    if (!sram)
        return NULL;
    sram->cells = calloc(1u << address_bits, sizeof *sram->cells);
    if (!sram->cells) {
        free(sram);
        return NULL;
    }
    memcpy(sram->ports, ports, sizeof ports);
    sram->words = ((unsigned)data_bits + 31) / 32;
    sram->address_mask = (1u << address_bits) - 1;
    if (!image_load(image, sram_image_write, sram)) {
        free(sram->cells);
        free(sram);
        return NULL;
    }
    s_cb_data cleanup = {0};
    cleanup.reason = cbEndOfSimulation;
    cleanup.cb_rtn = sram_free;
    cleanup.user_data = (PLI_BYTE8 *)sram;
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback) {
        free(sram->cells);
        free(sram);
        return NULL;
    }
    vpi_free_object(callback);
    vpi_put_userdata(call, sram);
    return sram;
}

static int scalar(vpiHandle port)
{
    s_vpi_value value = {0};
    value.format = vpiScalarVal;
    vpi_get_value(port, &value);
    return value.value.scalar;
}

/* The HDL adapter only connects pins. Storage and four-state bus behavior live here. */
static PLI_INT32 sram_update(PLI_BYTE8 *unused)
{
    (void)unused;
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    Sram *sram = vpi_get_userdata(call);
    if (!sram)
        sram = sram_open(call);
    if (!sram) {
        vpi_printf("Geany SRAM: invalid port configuration or allocation failed.\n");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }

    int chip = scalar(sram->ports[CHIP_N]);
    int write = scalar(sram->ports[WRITE_N]);
    int output = scalar(sram->ports[OUTPUT_N]);
    s_vpi_value value = {0};
    value.format = vpiVectorVal;
    vpi_get_value(sram->ports[ADDRESS], &value);
    unsigned address = (unsigned)value.value.vector[0].aval & sram->address_mask;
    SramCell *cell = (value.value.vector[0].bval & sram->address_mask) ? NULL : &sram->cells[address];
    s_vpi_vecval drive[2] = {{0, -1}, {0, -1}}; /* High impedance. */
    if (chip == vpi0 && write == vpi0 && cell) {
        vpi_get_value(sram->ports[DATA], &value);
        memcpy(cell->words, value.value.vector, sram->words * sizeof drive[0]);
        cell->written = 1;
    } else if (chip == vpi0 && write == vpi1 && output == vpi0) {
        if (cell && cell->written)
            memcpy(drive, cell->words, sram->words * sizeof drive[0]);
        else
            drive[0].aval = drive[1].aval = -1; /* Unknown contents. */
    }

    if (!sram->drive_valid || memcmp(sram->driven, drive, sram->words * sizeof drive[0])) {
        memcpy(sram->driven, drive, sram->words * sizeof drive[0]);
        sram->drive_valid = 1;
        value.value.vector = drive;
        vpi_put_value(sram->ports[DRIVE], &value, NULL, vpiNoDelay);
    }
    return 0;
}

enum {
    D_CK, D_RESET, D_CKE, D_CS, D_ACT, D_ADDRESS, D_BA, D_BG,
    D_DATA, D_DQS, D_MASK, D_READ_LATENCY, D_WRITE_LATENCY, D_DRIVE, D_STROBE, D_COUNT
};

typedef struct {
    Ddr4 device;
    vpiHandle ports[D_COUNT];
    vpiHandle cas_n, we_n;
    unsigned read_latency, write_latency, generation;
    int clock, strobe;
    s_vpi_vecval data_out;
    int strobe_out;
} Ddr4Vpi;

static PLI_INT32 ddr4_free(p_cb_data callback)
{
    free(callback->user_data);
    return 0;
}

static s_vpi_vecval vector_word(vpiHandle port)
{
    s_vpi_value value = {0};
    value.format = vpiVectorVal;
    vpi_get_value(port, &value);
    return value.value.vector[0];
}

/* EN: Store preload byte value at packed address in DDR state data. Return false out of range.
 * RU: Записывает байт value по упакованному address в DDR из data. При выходе за границы возвращает false. */
static int ddr_image_write(void *data, uint32_t address, uint64_t value)
{
    Ddr4Vpi *state = data;
    unsigned row_bits = state->generation == 1 ? 13 : state->generation == 2 ? 14 :
                        state->generation == 3 ? 16 : 17;
    if ((address >> 14) >= (1u << row_bits) ||
        (state->cas_n && (address & (1u << 13))) ||
        (state->generation == 1 && (address & (1u << 12))) || value > 255)
        return 0;
    Ddr4Cell *cell = ddr4_cell_key(&state->device, address, 1);
    if (!cell)
        return 0;
    cell->data = (uint8_t)value;
    cell->unknown = 0;
    return 1;
}

/* EN: Read ports from VPI call and create the model for generation, or return NULL.
 * RU: Читает порты VPI call и создаёт модель поколения generation либо возвращает NULL. */
static Ddr4Vpi *ddr_open(vpiHandle call, unsigned generation)
{
    int separate_commands = generation != 4;
    const int ddr3_order[] = {D_CK, D_RESET, D_CKE, D_CS, D_ACT, D_BG, D_COUNT,
        D_ADDRESS, D_BA, D_DATA, D_DQS, D_MASK, D_READ_LATENCY, D_WRITE_LATENCY,
        D_DRIVE, D_STROBE};
    const int ddr4_widths[] = {1, 1, 1, 1, 1, 17, 2, 2, 8, 1, 1, 0, 0, 8, 1};
    const int ddr3_widths[] = {1, 1, 1, 1, 1, 16, 3, 1, 8, 1, 1, 0, 0, 8, 1, 1};
    const int *widths = separate_commands ? ddr3_widths : ddr4_widths;
    vpiHandle pins[D_COUNT + 1] = {0};
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    unsigned count = D_COUNT + separate_commands;
    for (unsigned i = 0; i < count; i++) {
        unsigned pin = separate_commands ? (unsigned)ddr3_order[i] : i;
        if (generation <= 2 && pin == D_RESET)
            continue;
        pins[pin] = vpi_scan(args);
        if (!pins[pin])
            return NULL;
    }
    vpiHandle image = vpi_scan(args);
    if (image && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    for (unsigned i = 0; i < count; i++) {
        int width = widths[i];
        if (i == D_ADDRESS && generation <= 2)
            width = generation == 1 ? 13 : 14;
        if (i == D_BA && generation == 1)
            width = 2;
        if (pins[i] && width && vpi_get(vpiSize, pins[i]) != width)
            return NULL;
    }
    s_vpi_vecval rl = vector_word(pins[D_READ_LATENCY]);
    s_vpi_vecval wl = vector_word(pins[D_WRITE_LATENCY]);
    if (rl.bval || wl.bval || rl.aval < 1 || rl.aval > 64 || wl.aval < 1 || wl.aval > 64)
        return NULL;
    Ddr4Vpi *state = calloc(1, sizeof *state);
    if (!state)
        return NULL;
    memcpy(state->ports, pins, sizeof state->ports);
    state->generation = generation;
    state->device.burst = generation == 1 ? 4 : DDR4_BURST;
    if (separate_commands) {
        state->cas_n = pins[D_BG];
        state->we_n = pins[D_COUNT];
    }
    state->read_latency = rl.aval;
    state->write_latency = wl.aval;
    state->clock = state->strobe = vpiX;
    state->data_out.bval = 255;
    state->strobe_out = vpiZ;
    if (!image_load(image, ddr_image_write, state)) {
        free(state);
        return NULL;
    }
    s_cb_data cleanup = {0};
    cleanup.reason = cbEndOfSimulation;
    cleanup.cb_rtn = ddr4_free;
    cleanup.user_data = (PLI_BYTE8 *)state;
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback) {
        free(state);
        return NULL;
    }
    vpi_free_object(callback);
    vpi_put_userdata(call, state);
    return state;
}

static void ddr4_drive(Ddr4Vpi *state, s_vpi_vecval data, int strobe)
{
    s_vpi_value value = {0};
    if (data.aval != state->data_out.aval || data.bval != state->data_out.bval) {
        state->data_out = data;
        value.format = vpiVectorVal;
        value.value.vector = &data;
        vpi_put_value(state->ports[D_DRIVE], &value, NULL, vpiNoDelay);
    }
    if (strobe != state->strobe_out) {
        state->strobe_out = strobe;
        value.format = vpiScalarVal;
        value.value.scalar = strobe;
        vpi_put_value(state->ports[D_STROBE], &value, NULL, vpiNoDelay);
    }
}

static PLI_INT32 ddr_update(PLI_BYTE8 *user_data)
{
    unsigned generation = user_data ? (unsigned)*user_data : 4;
    int separate_commands = generation != 4;
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    Ddr4Vpi *state = vpi_get_userdata(call);
    if (!state)
        state = ddr_open(call, generation);
    if (!state) {
        vpi_printf("Geany DDR%u: invalid x8 port configuration or image.\n", generation);
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }

    Ddr4 *device = &state->device;
    int clock = scalar(state->ports[D_CK]);
    int strobe = scalar(state->ports[D_DQS]);
    int clock_edge = clock != state->clock && (clock == vpi0 || clock == vpi1);
    int strobe_edge = strobe != state->strobe && (strobe == vpi0 || strobe == vpi1);
    state->clock = clock;
    state->strobe = strobe;

    if (state->ports[D_RESET] && scalar(state->ports[D_RESET]) != vpi1) {
        ddr4_reset(device);
        ddr4_drive(state, (s_vpi_vecval){0, 255}, vpiZ);
        return 0;
    }
    if (clock_edge && clock == vpi1) {
        device->cycle++;
        if (scalar(state->ports[D_CKE]) == vpi1 && scalar(state->ports[D_CS]) == vpi0) {
            int activate = scalar(state->ports[D_ACT]);
            s_vpi_vecval address = vector_word(state->ports[D_ADDRESS]);
            s_vpi_vecval ba = vector_word(state->ports[D_BA]);
            unsigned bank = ba.aval & 7u;
            if (separate_commands) {
                int cas = scalar(state->cas_n), we = scalar(state->we_n);
                if ((activate != vpi0 && activate != vpi1) ||
                    (cas != vpi0 && cas != vpi1) || (we != vpi0 && we != vpi1)) {
                    activate = vpiX;
                } else {
                    unsigned command = ((activate == vpi1) << 2) | ((cas == vpi1) << 1) | (we == vpi1);
                    activate = command == 3 ? vpi0 : vpi1;
                    if (activate == vpi1) {
                        address.aval = (address.aval & 0x3fff) | (command << 14);
                        address.bval &= 0x3fff;
                    }
                }
            } else {
                s_vpi_vecval bg = vector_word(state->ports[D_BG]);
                bank = ((bg.aval & 3u) << 2) | (ba.aval & 3u);
                ba.bval |= bg.bval;
            }
            if ((activate == vpi0 || activate == vpi1) && !(address.bval | ba.bval))
                ddr4_command(device, activate == vpi1, address.aval & 131071u, bank,
                             state->read_latency, state->write_latency);
        }
    }
    if (device->mode == DDR4_READ && clock_edge && device->cycle >= device->due) {
        Ddr4Cell *cell = ddr4_cell(device, 0);
        s_vpi_vecval data = cell ? (s_vpi_vecval){cell->data, cell->unknown} :
                                  (s_vpi_vecval){255, 255};
        ddr4_drive(state, data, clock);
        ddr4_next_beat(device);
    } else if (clock_edge) {
        ddr4_drive(state, (s_vpi_vecval){0, 255}, vpiZ);
    }
    if (device->mode == DDR4_WRITE && strobe_edge && device->cycle >= device->due) {
        if (scalar(state->ports[D_MASK]) == (separate_commands ? vpi0 : vpi1)) {
            Ddr4Cell *cell = ddr4_cell(device, 1);
            if (cell) {
                s_vpi_vecval data = vector_word(state->ports[D_DATA]);
                cell->data = data.aval;
                cell->unknown = data.bval;
            } else {
                vpi_printf("Geany DDR%u: functional storage limit reached.\n", generation);
                vpi_control(vpiFinish, 1);
            }
        }
        ddr4_next_beat(device);
    }
    return 0;
}

enum { SPI_CLOCK, SPI_SELECT, SPI_MOSI, SPI_DRIVE, SPI_PORTS };

typedef struct {
    SpiMemory memory;
    vpiHandle ports[SPI_PORTS];
    unsigned bits, input;
    int clock, drive;
} SpiVpi;

static int spi_image_write(void *data, uint32_t address, uint64_t value)
{
    SpiMemory *memory = data;
    if (address >= memory->size || value > 255)
        return 0;
    memory->bytes[address] = value;
    return 1;
}

static PLI_INT32 spi_free(p_cb_data callback)
{
    SpiVpi *state = (SpiVpi *)callback->user_data;
    free(state->memory.bytes);
    free(state);
    return 0;
}

static SpiVpi *spi_open(vpiHandle call, unsigned kind)
{
    vpiHandle pins[SPI_PORTS];
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    for (unsigned i = 0; i < SPI_PORTS; i++) {
        pins[i] = vpi_scan(args);
        if (!pins[i])
            return NULL;
    }
    vpiHandle image = vpi_scan(args);
    if (image && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    for (unsigned i = 0; i < SPI_PORTS; i++)
        if (vpi_get(vpiSize, pins[i]) != 1)
            return NULL;
    SpiVpi *state = calloc(1, sizeof *state);
    if (!state)
        return NULL;
    memcpy(state->ports, pins, sizeof pins);
    if (!spi_memory_init(&state->memory, kind)) {
        free(state);
        return NULL;
    }
    state->clock = vpi0;
    state->drive = vpiZ;
    if (!image_load(image, spi_image_write, &state->memory)) {
        free(state->memory.bytes);
        free(state);
        return NULL;
    }
    s_cb_data cleanup = {0};
    cleanup.reason = cbEndOfSimulation;
    cleanup.cb_rtn = spi_free;
    cleanup.user_data = (PLI_BYTE8 *)state;
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback) {
        free(state->memory.bytes);
        free(state);
        return NULL;
    }
    vpi_free_object(callback);
    vpi_put_userdata(call, state);
    return state;
}

static PLI_INT32 spi_update(PLI_BYTE8 *user_data)
{
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    SpiVpi *state = vpi_get_userdata(call);
    if (!state)
        state = spi_open(call, (unsigned)*user_data);
    if (!state) {
        vpi_printf("Geany SPI: invalid port configuration or image.\n");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }
    int clock = scalar(state->ports[SPI_CLOCK]);
    int drive = state->drive;
    if (scalar(state->ports[SPI_SELECT]) != vpi0) {
        spi_memory_end(&state->memory);
        state->bits = state->input = 0;
        drive = vpiZ;
    } else if (state->clock == vpi0 && clock == vpi1) {
        state->input = (state->input << 1) | (scalar(state->ports[SPI_MOSI]) == vpi1);
        if (++state->bits == 8) {
            spi_memory_byte(&state->memory, state->input);
            state->bits = state->input = 0;
        }
    } else if (state->clock == vpi1 && clock == vpi0 && state->memory.count) {
        drive = (state->memory.output >> (7 - state->bits)) & 1 ? vpi1 : vpi0;
    }
    if (drive != state->drive) {
        s_vpi_value value = {0};
        value.format = vpiScalarVal;
        value.value.scalar = drive;
        vpi_put_value(state->ports[SPI_DRIVE], &value, NULL, vpiNoDelay);
        state->drive = drive;
    }
    state->clock = clock;
    return 0;
}

enum { I2C_CLOCK, I2C_SDA, I2C_TARGET, I2C_DRIVE, I2C_PORTS };

typedef struct {
    I2cEeprom device;
    vpiHandle ports[I2C_PORTS];
} I2cVpi;

static int i2c_image_write(void *data, uint32_t address, uint64_t value)
{
    I2cEeprom *device = data;
    if (address >= I2C_EEPROM_SIZE || value > 255)
        return 0;
    device->bytes[address] = value;
    return 1;
}

static PLI_INT32 i2c_free(p_cb_data callback)
{
    free(callback->user_data);
    return 0;
}

static I2cVpi *i2c_open(vpiHandle call)
{
    vpiHandle pins[I2C_PORTS];
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    for (unsigned i = 0; i < I2C_PORTS; i++) {
        pins[i] = vpi_scan(args);
        if (!pins[i])
            return NULL;
    }
    vpiHandle image = vpi_scan(args);
    if (image && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    for (unsigned i = 0; i < I2C_PORTS; i++)
        if (i != I2C_TARGET && vpi_get(vpiSize, pins[i]) != 1)
            return NULL;
    s_vpi_vecval target = vector_word(pins[I2C_TARGET]);
    if (target.bval || target.aval < 8 || target.aval > 119)
        return NULL;
    I2cVpi *state = calloc(1, sizeof *state);
    if (!state)
        return NULL;
    memcpy(state->ports, pins, sizeof pins);
    i2c_eeprom_init(&state->device, target.aval);
    if (!image_load(image, i2c_image_write, &state->device)) {
        free(state);
        return NULL;
    }
    s_cb_data cleanup = {0};
    cleanup.reason = cbEndOfSimulation;
    cleanup.cb_rtn = i2c_free;
    cleanup.user_data = (PLI_BYTE8 *)state;
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback) {
        free(state);
        return NULL;
    }
    vpi_free_object(callback);
    vpi_put_userdata(call, state);
    return state;
}

static PLI_INT32 i2c_update(PLI_BYTE8 *unused)
{
    (void)unused;
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    I2cVpi *state = vpi_get_userdata(call);
    if (!state)
        state = i2c_open(call);
    if (!state) {
        vpi_printf("Geany I2C: invalid EEPROM configuration or image.\n");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }
    int clock = scalar(state->ports[I2C_CLOCK]), data = scalar(state->ports[I2C_SDA]);
    if ((clock != vpi0 && clock != vpi1) || (data != vpi0 && data != vpi1))
        return 0;
    int previous = state->device.pull_low;
    if (i2c_eeprom_edge(&state->device, clock == vpi1, data == vpi1) != previous) {
        s_vpi_value value = {0};
        value.format = vpiScalarVal;
        value.value.scalar = state->device.pull_low ? vpi0 : vpiZ;
        vpi_put_value(state->ports[I2C_DRIVE], &value, NULL, vpiNoDelay);
    }
    return 0;
}

#include "geany-lua-bus.h"

#include "geany-sdram.h"

enum { SDR_CK, SDR_CKE, SDR_CS, SDR_RAS, SDR_CAS, SDR_WE, SDR_A, SDR_BA, SDR_DQ,
       SDR_DQM, SDR_OUT, SDR_PORTS };

typedef struct {
    Sdram ram;
    vpiHandle pins[SDR_PORTS];
    int clock;
} SdramDevice;

static int sdram_preload(void *data, uint32_t address, uint64_t value)
{
    if (address >= (1u << 24) || value > 0xffff)
        return 0;
    SdramWord *word = sdram_word(data, address, 1);
    if (!word)
        return 0;
    word->data = value;
    word->unknown = 0;
    return 1;
}

static PLI_INT32 sdram_free(p_cb_data callback)
{
    free(callback->user_data);
    return 0;
}

static SdramDevice *sdram_open(vpiHandle call)
{
    vpiHandle args = vpi_iterate(vpiArgument, call), pins[SDR_PORTS];
    if (!args)
        return NULL;
    const unsigned widths[] = {1, 1, 1, 1, 1, 1, 13, 2, 16, 2, 16};
    for (unsigned i = 0; i < SDR_PORTS; i++) {
        pins[i] = vpi_scan(args);
        if (!pins[i])
            return NULL;
    }
    vpiHandle image = vpi_scan(args);
    if (image && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    for (unsigned i = 0; i < SDR_PORTS; i++)
        if ((unsigned)vpi_get(vpiSize, pins[i]) != widths[i])
            return NULL;
    SdramDevice *device = calloc(1, sizeof *device);
    if (!device)
        return NULL;
    memcpy(device->pins, pins, sizeof pins);
    device->ram.burst = 1;
    device->ram.latency = 3;
    device->clock = vpiX;
    if (!image_load(image, sdram_preload, &device->ram)) {
        free(device);
        return NULL;
    }
    s_cb_data cleanup = {.reason = cbEndOfSimulation, .cb_rtn = sdram_free,
                         .user_data = (PLI_BYTE8 *)device};
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback) {
        free(device);
        return NULL;
    }
    vpi_free_object(callback);
    vpi_put_userdata(call, device);
    return device;
}

static PLI_INT32 sdram_update(PLI_BYTE8 *unused)
{
    (void)unused;
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    SdramDevice *device = vpi_get_userdata(call);
    if (!device)
        device = sdram_open(call);
    if (!device)
        goto fail;
    vpiHandle *p = device->pins;
    int previous = device->clock;
    device->clock = scalar(p[SDR_CK]);
    if (previous == vpi1 || device->clock != vpi1 || scalar(p[SDR_CKE]) != vpi1)
        return 0;
    Sdram *ram = &device->ram;
    ram->tick++;
    if (scalar(p[SDR_CS]) == vpi0) {
        int ras = scalar(p[SDR_RAS]), cas = scalar(p[SDR_CAS]), we = scalar(p[SDR_WE]);
        s_vpi_vecval address = vector_word(p[SDR_A]), bank = vector_word(p[SDR_BA]);
        if (ras <= vpi1 && cas <= vpi1 && we <= vpi1 && !(address.bval | bank.bval))
            sdram_command(ram, (ras << 2) | (cas << 1) | we, bank.aval & 3, address.aval & 8191);
    }
    s_vpi_vecval dqm = vector_word(p[SDR_DQM]), output = {0, 0xffff};
    unsigned mask = (dqm.aval | dqm.bval) & 3u;
    unsigned read_mask = ram->mask_history >> 2;
    ram->mask_history = ((ram->mask_history << 2) | mask) & 15u;
    if (ram->mode && ram->tick >= ram->due) {
        unsigned enabled = ram->mode == SDRAM_WRITE ? mask : read_mask;
        unsigned lanes = (enabled & 1 ? 0 : 0xff) | (enabled & 2 ? 0 : 0xff00);
        SdramWord *word = sdram_word(ram, sdram_address(ram), ram->mode == SDRAM_WRITE && lanes);
        if (ram->mode == SDRAM_READ) {
            output.aval = (word ? word->data : 0xffff) & lanes;
            output.bval = (word ? word->unknown & lanes : lanes) | (0xffffu & ~lanes);
        } else if (lanes) {
            if (!word)
                goto fail;
            s_vpi_vecval data = vector_word(p[SDR_DQ]);
            word->data = (word->data & ~lanes) | (data.aval & lanes);
            word->unknown = (word->unknown & ~lanes) | (data.bval & lanes);
        }
        if (++ram->beat == ram->length)
            sdram_end(ram);
    }
    s_vpi_value value = {.format = vpiVectorVal};
    value.value.vector = &output;
    vpi_put_value(p[SDR_OUT], &value, NULL, vpiNoDelay);
    return 0;

fail:
    vpi_printf("Geany SDRAM: invalid configuration, image or storage limit reached.\n");
    vpip_set_return_value(1);
    vpi_control(vpiFinish, 1);
    return 0;
}

#include "geany-bus-controller.h"
#include "geany-usart.h"
#include "geany-parallel-psram.h"
#include "geany-ddr5.h"
#include "geany-raster.h"

/* EN: Register all built-in VPI system tasks at simulator startup.
 * RU: Регистрирует системные задачи встроенных устройств при запуске симулятора. */
static void register_devices(void)
{
    s_vpi_systf_data task = {0};
    task.type = vpiSysTask;
    task.tfname = "$geany_sram";
    task.calltf = sram_update;
    vpi_register_systf(&task);
    task.tfname = "$geany_ddr4";
    task.calltf = ddr_update;
    vpi_register_systf(&task);
    static PLI_BYTE8 ddr3 = 3, ddr2 = 2, ddr1 = 1;
    task.tfname = "$geany_ddr3";
    task.user_data = &ddr3;
    vpi_register_systf(&task);
    task.tfname = "$geany_ddr2";
    task.user_data = &ddr2;
    vpi_register_systf(&task);
    task.tfname = "$geany_ddr1";
    task.user_data = &ddr1;
    vpi_register_systf(&task);
    static PLI_BYTE8 nor = SPI_NOR, nand = SPI_NAND, eeprom = SPI_EEPROM, psram = SPI_PSRAM;
    task.user_data = NULL;
    task.tfname = "$geany_sdram";
    task.calltf = sdram_update;
    vpi_register_systf(&task);
    task.calltf = spi_update;
    task.tfname = "$geany_spi_nor";
    task.user_data = &nor;
    vpi_register_systf(&task);
    task.tfname = "$geany_spi_nand";
    task.user_data = &nand;
    vpi_register_systf(&task);
    task.tfname = "$geany_spi_eeprom";
    task.user_data = &eeprom;
    vpi_register_systf(&task);
    task.tfname = "$geany_spi_psram";
    task.user_data = &psram;
    vpi_register_systf(&task);
    task.tfname = "$geany_i2c_eeprom";
    task.calltf = i2c_update;
    task.user_data = NULL;
    vpi_register_systf(&task);
    static PLI_BYTE8 i2c_lua = 1;
    task.tfname = "$geany_spi_lua";
    task.calltf = lua_bus_update;
    vpi_register_systf(&task);
    task.tfname = "$geany_i2c_lua";
    task.user_data = &i2c_lua;
    vpi_register_systf(&task);
    task.tfname = "$geany_i2c_controller";
    task.calltf = bus_controller_tick;
    vpi_register_systf(&task);
    task.tfname = "$geany_spi_controller";
    task.user_data = NULL;
    vpi_register_systf(&task);
    task.tfname = "$geany_usart";
    task.calltf = geany_usart_tick;
    vpi_register_systf(&task);
    task.tfname = "$geany_parallel_psram";
    task.calltf = parallel_psram_update;
    vpi_register_systf(&task);
    task.tfname = "$geany_ddr5";
    task.calltf = ddr5_update;
    vpi_register_systf(&task);
    task.tfname = "$geany_raster";
    task.calltf = raster_tick;
    vpi_register_systf(&task);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
void (*vlog_startup_routines[])(void) = {register_devices, NULL};
