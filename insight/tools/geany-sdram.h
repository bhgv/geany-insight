/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_SDRAM_H
#define GEANY_SDRAM_H

#include <stdint.h>

enum { SDRAM_CELLS = 65536 };
enum { SDRAM_IDLE, SDRAM_READ, SDRAM_WRITE };

typedef struct {
    uint32_t tag;
    uint16_t data, unknown;
} SdramWord;

typedef struct {
    SdramWord words[SDRAM_CELLS];
    unsigned row[4], open, bank, column, beat, mode, close_row;
    unsigned burst, length, latency, single_write, mask_history;
    uint64_t tick, due;
} Sdram;

static SdramWord *sdram_word(Sdram *ram, uint32_t address, int create)
{
    unsigned slot = (address * UINT32_C(2654435761)) >> 16;
    for (unsigned i = 0; i < SDRAM_CELLS; i++) {
        SdramWord *word = &ram->words[(slot + i) & (SDRAM_CELLS - 1)];
        if (word->tag == address + 1)
            return word;
        if (!word->tag) {
            if (!create)
                return NULL;
            word->tag = address + 1;
            word->unknown = 0xffff;
            return word;
        }
    }
    return NULL;
}

static void sdram_end(Sdram *ram)
{
    if (ram->mode && ram->close_row)
        ram->open &= ~(1u << ram->bank);
    ram->mode = SDRAM_IDLE;
}

static void sdram_command(Sdram *ram, unsigned command, unsigned bank, unsigned address)
{
    unsigned mask = 1u << bank;
    switch (command) {
    case 0: { /* LOAD MODE REGISTER */
        unsigned burst = address & 7u, latency = (address >> 4) & 7u;
        if (!ram->open && bank == 0 && burst <= 3 && (latency == 2 || latency == 3) &&
            !(address & 0x1d88u)) {
            ram->burst = 1u << burst;
            ram->latency = latency;
            ram->single_write = (address >> 9) & 1u;
        }
        break;
    }
    case 2: /* PRECHARGE */
        mask = address & 1024 ? 15u : mask;
        if (ram->mode && (mask & (1u << ram->bank)))
            sdram_end(ram);
        ram->open &= ~mask;
        break;
    case 3: /* ACTIVATE */
        if (!(ram->open & mask)) {
            ram->row[bank] = address;
            ram->open |= mask;
        }
        break;
    case 4: case 5: /* WRITE / READ */
        if (!ram->mode && (ram->open & mask)) {
            ram->mode = command == 4 ? SDRAM_WRITE : SDRAM_READ;
            ram->bank = bank;
            ram->column = address & 511u;
            ram->close_row = (address >> 10) & 1u;
            ram->length = command == 4 && ram->single_write ? 1 : ram->burst;
            ram->beat = 0;
            ram->due = ram->tick + (command == 5 ? ram->latency : 0);
        }
        break;
    case 6: /* BURST TERMINATE */
        sdram_end(ram);
        break;
    }
}

static uint32_t sdram_address(const Sdram *ram)
{
    unsigned mask = ram->burst - 1;
    unsigned column = (ram->column & ~mask) | ((ram->column + ram->beat) & mask);
    return (ram->row[ram->bank] << 11) | (ram->bank << 9) | column;
}
#endif
