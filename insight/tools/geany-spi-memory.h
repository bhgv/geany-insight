/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Emulate SPI flash, EEPROM and PSRAM and expose committed byte writes.
 * RU: Эмулирует SPI-флеш, EEPROM и PSRAM и сообщает о завершённых записях байтов. */
#ifndef GEANY_SPI_MEMORY_H
#define GEANY_SPI_MEMORY_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { SPI_NOR = 1, SPI_NAND, SPI_EEPROM, SPI_PSRAM };
enum { SPI_NOR_SIZE = 65536, SPI_EEPROM_SIZE = 65536, SPI_NAND_PAGE = 2048,
       SPI_NAND_SIZE = 64 * SPI_NAND_PAGE, SPI_PSRAM_SIZE = 8 * 1024 * 1024 };

typedef struct {
    unsigned kind, size, page_size, wel, command, count, address, output, dirty;
    unsigned wrap, reset_enabled;
    uint8_t *bytes, cache[SPI_NAND_PAGE];
    void (*changed)(void *, unsigned, uint8_t, uint8_t);
    void *inspection;
} SpiMemory;

static int spi_memory_init(SpiMemory *m, unsigned kind)
{
    m->kind = kind;
    m->size = kind == SPI_PSRAM ? SPI_PSRAM_SIZE : kind == SPI_NAND ? SPI_NAND_SIZE :
              kind == SPI_NOR ? SPI_NOR_SIZE : SPI_EEPROM_SIZE;
    m->bytes = malloc(m->size);
    if (!m->bytes)
        return 0;
    m->page_size = kind == SPI_NAND ? SPI_NAND_PAGE : kind == SPI_NOR ? 256 : 128;
    memset(m->bytes, 255, m->size);
    memset(m->cache, 255, sizeof m->cache);
    return 1;
}

static void spi_memory_end(SpiMemory *m)
{
    if (m->kind == SPI_PSRAM) {
        if (m->count) {
            if (m->command == 0xc0)
                m->wrap = !m->wrap;
            else if (m->command == 0x99 && m->reset_enabled)
                m->wrap = 0;
            m->reset_enabled = m->command == 0x66;
        }
    } else if (m->count && m->command == 0x06)
        m->wel = 1;
    else if (m->count && m->command == 0x04)
        m->wel = 0;
    else if (m->dirty)
        m->wel = 0;
    m->count = m->address = m->dirty = 0;
    m->output = 255;
}

/* EN: Store value at address in m and notify its observer if it changed.
 * RU: Записывает value по address в m и уведомляет наблюдателя об изменении. */
static void spi_memory_store(SpiMemory *m, unsigned address, uint8_t value)
{
    uint8_t before = m->bytes[address];
    m->bytes[address] = value;
    if (m->changed && before != value)
        m->changed(m->inspection, address, before, value);
}

/* EN: Erase count bytes of m starting at address.
 * RU: Стирает count байтов m начиная с address. */
static void spi_memory_erase(SpiMemory *m, unsigned address, unsigned count)
{
    if (!m->changed)
    {
        memset(m->bytes + address, 255, count);
        return;
    }
    for (unsigned i = 0; i < count; ++i)
        spi_memory_store(m, address + i, 255);
}

/* EN: Process byte index of a NAND command in m.
 * RU: Обрабатывает байт byte с индексом index команды NAND в m. */
static void spi_memory_nand(SpiMemory *m, unsigned index, unsigned byte)
{
    switch (m->command) {
    case 0x0f: /* Get feature register */
        if (index == 1)
            m->address = byte;
        m->output = m->address == 0xc0 && m->wel ? 2 : 0;
        break;
    case 0x13: case 0x10: case 0xd8: /* Page to cache, program execute, block erase */
        if (index > 3)
            break;
        m->address = (m->address << 8) | byte;
        if (index == 3 && m->address < m->size / m->page_size) {
            uint8_t *page = m->bytes + m->address * m->page_size;
            if (m->command == 0x13)
                memcpy(m->cache, page, m->page_size);
            else if (m->wel) {
                if (m->command == 0xd8)
                    spi_memory_erase(m, 0, m->size); /* One 64-page block */
                else
                    for (unsigned i = 0; i < m->page_size; i++)
                        spi_memory_store(m, m->address * m->page_size + i, page[i] & m->cache[i]);
                m->dirty = 1;
            }
        } else if (index == 3 && m->command == 0x13)
            memset(m->cache, 255, m->page_size);
        break;
    case 0x03: case 0x02: /* Read cache, program load */
        if (index <= 2)
            m->address = (m->address << 8) | byte;
        else if (m->command == 0x03)
            m->output = m->address < m->page_size ? m->cache[m->address++] : 255;
        else if (m->address < m->page_size)
            m->cache[m->address++] = byte;
        break;
    }
}

/* EN: Process byte index of a PSRAM command in m.
 * RU: Обрабатывает байт byte с индексом index команды PSRAM в m. */
static void spi_memory_psram(SpiMemory *m, unsigned index, unsigned byte)
{
    if (m->command != 0x02 && m->command != 0x03 && m->command != 0x0b)
        return;
    if (index <= 3) {
        m->address = ((m->address << 8) | byte) & (SPI_PSRAM_SIZE - 1);
        if (index < 3 || m->command != 0x03)
            return;
    }
    if (m->command == 0x02)
        spi_memory_store(m, m->address, byte);
    else
        m->output = m->bytes[m->address];
    unsigned mask = m->wrap ? 31u : SPI_PSRAM_SIZE - 1u;
    m->address = (m->address & ~mask) | ((m->address + 1) & mask);
}

/* EN: Advance m by one byte received on the SPI bus.
 * RU: Продвигает состояние m после получения байта byte по SPI. */
static void spi_memory_byte(SpiMemory *m, unsigned byte)
{
    unsigned index = m->count++;
    m->output = 255;
    if (!index) {
        m->command = byte;
        if (m->kind == SPI_NAND && byte == 0x02)
            memset(m->cache, 255, m->page_size);
        if (m->kind == SPI_NAND && byte == 0xff) {
            m->wel = 0;
            memset(m->cache, 255, m->page_size);
        }
        if (m->kind != SPI_NAND && m->kind != SPI_PSRAM && byte == 0x05)
            m->output = m->wel ? 2 : 0;
        return;
    }
    if (m->kind == SPI_PSRAM) {
        spi_memory_psram(m, index, byte);
        return;
    }
    if (m->kind == SPI_NAND) {
        spi_memory_nand(m, index, byte);
        return;
    }
    if (m->command == 0x05) {
        m->output = m->wel ? 2 : 0;
        return;
    }
    if (m->command != 0x03 && m->command != 0x02 && (m->kind != SPI_NOR || m->command != 0x20))
        return;
    unsigned address_bytes = m->kind == SPI_NOR ? 3 : 2;
    if (index <= address_bytes) {
        m->address = (m->address << 8) | byte;
        if (index < address_bytes)
            return;
        if (m->command == 0x20 && m->wel && m->address < m->size) {
            spi_memory_erase(m, m->address & ~4095u, 4096);
            m->dirty = 1;
        }
    }
    if (m->command == 0x03)
        m->output = m->address < m->size ? m->bytes[m->address++] : 255;
    else if (m->command == 0x02 && index > address_bytes && m->wel && m->address < m->size) {
        if (m->kind == SPI_NOR)
            spi_memory_store(m, m->address, m->bytes[m->address] & byte);
        else
            spi_memory_store(m, m->address, byte);
        unsigned mask = m->page_size - 1;
        m->address = (m->address & ~mask) | ((m->address + 1) & mask);
        m->dirty = 1;
    }
}

#endif
