/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Decode and format dump words, calculate values and retain recent writes.
 * RU: Читает и форматирует слова дампа, вычисляет значения и хранит недавние записи. */
#ifndef INSIGHT_MEMORY_VALUES_H
#define INSIGHT_MEMORY_VALUES_H

#include <stdbool.h>
#include <stdint.h>

enum { MEMORY_VALUE_TEXT_SIZE = 65, MEMORY_HISTORY_LIMIT = 10 };

typedef enum
{
    MEMORY_ADD, MEMORY_SUBTRACT, MEMORY_MULTIPLY, MEMORY_DIVIDE,
    MEMORY_AND, MEMORY_OR, MEMORY_XOR, MEMORY_NOT
} MemoryOperation;

typedef struct
{
    uint64_t time, address, before, after;
    uint64_t before_unknown, after_unknown;
} MemoryChange;

typedef struct
{
    MemoryChange entries[MEMORY_HISTORY_LIMIT];
    unsigned next, count;
} MemoryHistory;

/* EN: Decode count bytes (1, 2, 4 or 8) from bytes using big_endian byte order.
 * RU: Собирает count байтов (1, 2, 4 или 8) из bytes в порядке big_endian. */
static inline uint64_t memory_word_read(const uint8_t *bytes, unsigned count, bool big_endian)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        unsigned index = big_endian ? i : count - 1 - i;
        value = (value << 8) | bytes[index];
    }
    return value;
}

/* EN: Write value in base (2, 10 or 16) to text, padding binary and hex to bits.
 * RU: Записывает value в text в системе base (2, 10 или 16), дополняя двоичную
 * и шестнадцатеричную запись нулями до разрядности bits. */
static inline void memory_word_format(char text[MEMORY_VALUE_TEXT_SIZE], uint64_t value,
                                      unsigned base, unsigned bits)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned minimum = base == 2 ? bits : base == 16 ? (bits + 3) / 4 : 1;
    unsigned length = 0;
    do
    {
        text[length++] = digits[value % base];
        value /= base;
    }
    while (value || length < minimum);

    text[length] = '\0';
    for (unsigned i = 0; i < length / 2; ++i)
    {
        char digit = text[i];
        text[i] = text[length - 1 - i];
        text[length - 1 - i] = digit;
    }
}

/* EN: Apply operation to left and right using unsigned 64-bit arithmetic.
 * Write result and return true on success. Division by zero leaves result intact.
 * RU: Выполняет operation над left и right в беззнаковой 64-битной арифметике.
 * При успехе записывает result и возвращает true. При делении на ноль result не меняется. */
static inline bool memory_calculate(MemoryOperation operation, uint64_t left,
                                    uint64_t right, uint64_t *result)
{
    switch (operation)
    {
        case MEMORY_ADD: *result = left + right; break;
        case MEMORY_SUBTRACT: *result = left - right; break;
        case MEMORY_MULTIPLY: *result = left * right; break;
        case MEMORY_DIVIDE:
            if (!right)
                return false;
            *result = left / right;
            break;
        case MEMORY_AND: *result = left & right; break;
        case MEMORY_OR: *result = left | right; break;
        case MEMORY_XOR: *result = left ^ right; break;
        case MEMORY_NOT: *result = ~left; break;
        default: return false;
    }
    return true;
}

/* EN: Append change to zero-initialized history if its value or unknown mask changed.
 * RU: Добавляет change в обнулённую history при изменении значения или маски неизвестных битов. */
static inline void memory_history_record(MemoryHistory *history, MemoryChange change)
{
    if (change.before == change.after && change.before_unknown == change.after_unknown)
        return;

    history->entries[history->next] = change;
    history->next = (history->next + 1) % MEMORY_HISTORY_LIMIT;
    if (history->count < MEMORY_HISTORY_LIMIT)
        ++history->count;
}

/* EN: Return the entry age writes back in history (zero is newest), or NULL if absent.
 * RU: Возвращает запись history на age изменений назад (ноль — последняя) или NULL. */
static inline const MemoryChange *memory_history_at(const MemoryHistory *history, unsigned age)
{
    if (age >= history->count)
        return 0;
    unsigned index = (history->next + MEMORY_HISTORY_LIMIT - 1 - age) % MEMORY_HISTORY_LIMIT;
    return &history->entries[index];
}

#endif
