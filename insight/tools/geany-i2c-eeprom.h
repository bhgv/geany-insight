/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: I2C EEPROM transport shared by writable memory and read-only EDID.
 * RU: Транспорт I2C для записываемой памяти и EDID только для чтения. */
#ifndef GEANY_I2C_EEPROM_H
#define GEANY_I2C_EEPROM_H

#include <stdint.h>
#include <string.h>

enum { I2C_IDLE, I2C_RECEIVE, I2C_ACK, I2C_SEND, I2C_MASTER_ACK };
enum { I2C_SELECT, I2C_ADDRESS_HIGH, I2C_ADDRESS_LOW, I2C_DATA };
enum { I2C_EEPROM_SIZE = 65536, I2C_EEPROM_PAGE = 128 };

typedef struct {
    uint8_t bytes[I2C_EEPROM_SIZE];
    uint8_t page[I2C_EEPROM_PAGE], dirty[I2C_EEPROM_PAGE];
    unsigned target, address, stage, phase, next, bits, input;
    int clock, data, pull_low, read_only;
    void (*changed)(void *, unsigned, uint8_t, uint8_t);
    void *inspection;
} I2cEeprom;

static void i2c_eeprom_init(I2cEeprom *device, unsigned address)
{
    memset(device->bytes, 255, sizeof device->bytes);
    device->target = address;
    device->clock = device->data = 1;
}

/* EN: Apply the received byte to device and return its post-ACK phase.
 * RU: Обрабатывает принятый байт device и возвращает фазу после ACK. */
static unsigned i2c_eeprom_receive(I2cEeprom *device)
{
    unsigned byte = device->input;
    switch (device->stage) {
    case I2C_SELECT:
        if ((byte >> 1) != device->target)
            return I2C_IDLE;
        device->stage = device->read_only ? I2C_ADDRESS_LOW : I2C_ADDRESS_HIGH;
        return byte & 1 ? I2C_SEND : I2C_RECEIVE;
    case I2C_ADDRESS_HIGH:
        device->address = byte << 8;
        device->stage = I2C_ADDRESS_LOW;
        break;
    case I2C_ADDRESS_LOW:
        device->address = device->read_only ? byte : device->address | byte;
        device->stage = I2C_DATA;
        break;
    case I2C_DATA: {
        if (device->read_only)
            return I2C_IDLE;
        unsigned mask = I2C_EEPROM_PAGE - 1;
        unsigned offset = device->address & mask;
        device->page[offset] = byte;
        device->dirty[offset] = 1;
        device->address = (device->address & ~mask) | ((device->address + 1) & mask);
        break;
    }
    }
    return I2C_RECEIVE;
}

/* EN: Update device for SCL clock and SDA data. Return whether to drive SDA low.
 * RU: Обновляет device по SCL clock и SDA data. Возвращает признак притяжки SDA к нулю. */
static int i2c_eeprom_edge(I2cEeprom *device, int clock, int data)
{
    if (clock && device->clock && data != device->data) {
        if (data && !device->read_only && device->stage == I2C_DATA) {
            unsigned base = device->address & ~(I2C_EEPROM_PAGE - 1u);
            for (unsigned i = 0; i < I2C_EEPROM_PAGE; i++)
            {
                if (!device->dirty[i])
                    continue;
                uint8_t before = device->bytes[base + i];
                uint8_t after = device->page[i];
                device->bytes[base + i] = after;
                if (device->changed && before != after)
                    device->changed(device->inspection, base + i, before, after);
            }
        }
        memset(device->dirty, 0, sizeof device->dirty);
        device->phase = data ? I2C_IDLE : I2C_RECEIVE;
        device->stage = I2C_SELECT;
        device->bits = device->input = 0;
        device->pull_low = 0;
    } else if (clock && !device->clock) {
        switch (device->phase) {
        case I2C_RECEIVE:
            device->input = (device->input << 1) | (unsigned)data;
            if (++device->bits == 8) {
                device->next = i2c_eeprom_receive(device);
                device->phase = I2C_ACK;
                device->bits = device->input = 0;
            }
            break;
        case I2C_SEND:
            if (++device->bits == 8) {
                device->phase = I2C_MASTER_ACK;
                device->bits = 0;
                device->address = (device->address + 1) & (device->read_only ? 255u : 65535u);
            }
            break;
        case I2C_MASTER_ACK:
            device->next = data ? I2C_IDLE : I2C_SEND;
            device->bits = 1;
            break;
        case I2C_ACK:
            device->bits = 1;
            break;
        }
    } else if (!clock && device->clock) {
        if ((device->phase == I2C_ACK || device->phase == I2C_MASTER_ACK) && device->bits) {
            device->phase = device->next;
            device->bits = 0;
        }
        device->pull_low = device->phase == I2C_ACK && device->next != I2C_IDLE;
        if (device->phase == I2C_SEND)
            device->pull_low = !(device->bytes[device->address] & (128u >> device->bits));
    }
    device->clock = clock;
    device->data = data;
    return device->pull_low;
}

#endif
