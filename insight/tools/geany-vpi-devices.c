/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <vpi_user.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
    if (vpi_scan(args)) {
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

static void register_devices(void)
{
    s_vpi_systf_data task = {0};
    task.type = vpiSysTask;
    task.tfname = "$geany_sram";
    task.calltf = sram_update;
    vpi_register_systf(&task);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
void (*vlog_startup_routines[])(void) = {register_devices, NULL};
