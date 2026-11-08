/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Shared functional burst storage and command handling for DDR models.
 * RU: Общая память пакетов и обработка команд функциональных моделей DDR. */
#ifndef GEANY_DDR4_H
#define GEANY_DDR4_H

#include <stdint.h>
#include <stddef.h>

/* Functional x8 model. One rank, one configured burst length, one outstanding burst. */
enum { DDR4_BANKS = 16, DDR4_CAPACITY = 65536, DDR4_BURST = 8 };
enum { DDR4_IDLE, DDR4_READ, DDR4_WRITE };

typedef struct {
    uint32_t key;
    uint8_t data, unknown, used;
} Ddr4Cell;

typedef struct {
    Ddr4Cell cells[DDR4_CAPACITY];
    uint32_t row[DDR4_BANKS];
    uint64_t cycle, due;
    unsigned open, mode, bank, column, beat, burst, auto_close;
} Ddr4;

static Ddr4Cell *ddr4_cell_key(Ddr4 *device, uint32_t key, int create)
{
    unsigned slot = (key * UINT32_C(2654435761)) >> 16;
    for (unsigned i = 0; i < DDR4_CAPACITY; i++) {
        Ddr4Cell *cell = &device->cells[(slot + i) & (DDR4_CAPACITY - 1)];
        if (cell->used && cell->key == key)
            return cell;
        if (!cell->used) {
            if (!create)
                return NULL;
            cell->used = 1;
            cell->key = key;
            return cell;
        }
    }
    return NULL;
}

/* EN: Find the current burst cell in device. create allocates a missing cell.
 * RU: Ищет ячейку текущего пакета device. create разрешает создать ячейку. */
static Ddr4Cell *ddr4_cell(Ddr4 *device, int create)
{
    unsigned mask = device->burst - 1;
    unsigned column = (device->column & ~mask) | ((device->column + device->beat) & mask);
    uint32_t key = (device->row[device->bank] << 14) | (device->bank << 10) | column;
    return ddr4_cell_key(device, key, create);
}

static void ddr4_reset(Ddr4 *device)
{
    device->cycle = 0;
    device->open = 0;
    device->mode = DDR4_IDLE;
}

static void ddr4_command(Ddr4 *device, unsigned activate_n, unsigned address,
                         unsigned bank, unsigned read_latency, unsigned write_latency)
{
    unsigned mask = 1u << bank;
    if (!activate_n) {
        if (!(device->open & mask)) {
            device->row[bank] = address;
            device->open |= mask;
        }
        return;
    }
    unsigned command = (address >> 14) & 7;
    if (command == 2) { /* PRE */
        device->open &= address & (1u << 10) ? 0 : ~mask;
        return;
    }
    if ((command != 4 && command != 5) || !(device->open & mask) || device->mode)
        return;
    device->mode = command == 5 ? DDR4_READ : DDR4_WRITE;
    device->bank = bank;
    device->column = address & 1023;
    device->auto_close = (address >> 10) & 1;
    device->beat = 0;
    device->due = device->cycle + (device->mode == DDR4_READ ? read_latency : write_latency);
}

/* EN: Advance device by one beat and apply auto-precharge at burst end.
 * RU: Продвигает device на один такт данных и закрывает банк при автопредзаряде. */
static void ddr4_next_beat(Ddr4 *device)
{
    if (++device->beat == device->burst) {
        if (device->auto_close)
            device->open &= ~(1u << device->bank);
        device->mode = DDR4_IDLE;
    }
}

#endif
