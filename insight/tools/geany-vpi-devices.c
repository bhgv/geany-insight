/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <vpi_user.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "geany-ddr4.h"

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
    unsigned read_latency, write_latency;
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

static int ddr_image_write(void *data, uint32_t address, uint64_t value)
{
    Ddr4Vpi *state = data;
    if (address > (state->cas_n ? UINT32_C(0x3fffffff) : UINT32_C(0x7fffffff)) ||
        (state->cas_n && (address & (1u << 13))) || value > 255)
        return 0;
    Ddr4Cell *cell = ddr4_cell_key(&state->device, address, 1);
    if (!cell)
        return 0;
    cell->data = (uint8_t)value;
    cell->unknown = 0;
    return 1;
}

static Ddr4Vpi *ddr_open(vpiHandle call, int ddr3)
{
    const int ddr3_order[] = {D_CK, D_RESET, D_CKE, D_CS, D_ACT, D_BG, D_COUNT,
        D_ADDRESS, D_BA, D_DATA, D_DQS, D_MASK, D_READ_LATENCY, D_WRITE_LATENCY,
        D_DRIVE, D_STROBE};
    const int ddr4_widths[] = {1, 1, 1, 1, 1, 17, 2, 2, 8, 1, 1, 0, 0, 8, 1};
    const int ddr3_widths[] = {1, 1, 1, 1, 1, 16, 3, 1, 8, 1, 1, 0, 0, 8, 1, 1};
    const int *widths = ddr3 ? ddr3_widths : ddr4_widths;
    vpiHandle pins[D_COUNT + 1] = {0};
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    for (unsigned i = 0; i < D_COUNT + (unsigned)ddr3; i++) {
        unsigned pin = ddr3 ? (unsigned)ddr3_order[i] : i;
        pins[pin] = vpi_scan(args);
        if (!pins[pin])
            return NULL;
    }
    vpiHandle image = vpi_scan(args);
    if (image && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    for (unsigned i = 0; i < D_COUNT + (unsigned)ddr3; i++)
        if (widths[i] && vpi_get(vpiSize, pins[i]) != widths[i])
            return NULL;
    s_vpi_vecval rl = vector_word(pins[D_READ_LATENCY]);
    s_vpi_vecval wl = vector_word(pins[D_WRITE_LATENCY]);
    if (rl.bval || wl.bval || rl.aval < 1 || rl.aval > 64 || wl.aval < 1 || wl.aval > 64)
        return NULL;
    Ddr4Vpi *state = calloc(1, sizeof *state);
    if (!state)
        return NULL;
    memcpy(state->ports, pins, sizeof state->ports);
    if (ddr3) {
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
    int ddr3 = user_data != NULL;
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    Ddr4Vpi *state = vpi_get_userdata(call);
    if (!state)
        state = ddr_open(call, ddr3);
    if (!state) {
        vpi_printf("Geany DDR%d: invalid x8 port configuration or image.\n", ddr3 ? 3 : 4);
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

    if (scalar(state->ports[D_RESET]) != vpi1) {
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
            if (ddr3) {
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
        if (scalar(state->ports[D_MASK]) == (ddr3 ? vpi0 : vpi1)) {
            Ddr4Cell *cell = ddr4_cell(device, 1);
            if (cell) {
                s_vpi_vecval data = vector_word(state->ports[D_DATA]);
                cell->data = data.aval;
                cell->unknown = data.bval;
            } else {
                vpi_printf("Geany DDR%d: functional storage limit reached.\n", ddr3 ? 3 : 4);
                vpi_control(vpiFinish, 1);
            }
        }
        ddr4_next_beat(device);
    }
    return 0;
}

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
    static PLI_BYTE8 ddr3 = 1;
    task.tfname = "$geany_ddr3";
    task.user_data = &ddr3;
    vpi_register_systf(&task);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
void (*vlog_startup_routines[])(void) = {register_devices, NULL};
