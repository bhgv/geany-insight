/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Functional asynchronous x16 PSRAM with byte enables and image preload.
 * RU: Асинхронная функциональная PSRAM x16 с выбором байтов и загрузкой образа.
 */

enum {
    PSRAM_ADDRESS, PSRAM_DQ, PSRAM_CE_N, PSRAM_WE_N, PSRAM_OE_N,
    PSRAM_LB_N, PSRAM_UB_N, PSRAM_DRIVE, PSRAM_PORTS
};

typedef struct {
    uint16_t data, unknown, written;
} ParallelPsramWord;

typedef struct {
    vpiHandle pins[PSRAM_PORTS];
    ParallelPsramWord *words;
    unsigned size;
    s_vpi_vecval driven, write_data;
    unsigned write_address, write_mask;
    int drive_valid, writing;
    MemoryInspection inspection;
} ParallelPsram;

/* EN: Store value at word address in PSRAM data, rejecting invalid image words.
 * RU: Записывает value по адресу слова address в PSRAM из data с проверкой границ. */
static int parallel_psram_image(void *data, uint32_t address, uint64_t value)
{
    ParallelPsram *memory = data;
    if (address >= memory->size || value > UINT16_MAX)
        return 0;
    memory->words[address] = (ParallelPsramWord){(uint16_t)value, 0, UINT16_MAX};
    return 1;
}

/* EN: Release memory in callback at the end of simulation.
 * RU: Освобождает память из callback при завершении симуляции. */
static PLI_INT32 parallel_psram_free(p_cb_data callback)
{
    ParallelPsram *memory = (ParallelPsram *)callback->user_data;
    FILE *file = memory->inspection.file;
    if (file)
    {
        fprintf(file, "GEANY-MEMORY 1 PSRAM %u 16\n", memory->size);
        for (unsigned address = 0; address < memory->size; ++address)
        {
            const ParallelPsramWord *word = &memory->words[address];
            if (word->written)
            {
                unsigned unwritten = UINT16_MAX ^ word->written;
                fprintf(file, "W %x %x %x\n", address, word->data | unwritten,
                        word->unknown | unwritten);
            }
        }
        memory_inspection_close(&memory->inspection);
    }
    free(memory->words);
    free(memory);
    return 0;
}

/* EN: Bind pins of call and load the optional image. Return NULL on failure.
 * RU: Подключает выводы call и загружает необязательный образ. При ошибке возвращает NULL. */
static ParallelPsram *parallel_psram_open(vpiHandle call)
{
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    vpiHandle pins[PSRAM_PORTS];
    for (unsigned i = 0; i < PSRAM_PORTS; ++i) {
        pins[i] = vpi_scan(args);
        if (!pins[i])
            return NULL;
    }
    vpiHandle image = vpi_scan(args);
    if (image && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    int address_bits = vpi_get(vpiSize, pins[PSRAM_ADDRESS]);
    if (address_bits < 1 || address_bits > 20 ||
        vpi_get(vpiSize, pins[PSRAM_DQ]) != 16 || vpi_get(vpiSize, pins[PSRAM_DRIVE]) != 16)
        return NULL;
    for (unsigned i = PSRAM_CE_N; i <= PSRAM_UB_N; ++i)
        if (vpi_get(vpiSize, pins[i]) != 1)
            return NULL;

    ParallelPsram *memory = calloc(1, sizeof *memory);
    if (!memory)
        return NULL;
    memory->size = 1u << address_bits;
    memory->words = calloc(memory->size, sizeof *memory->words);
    if (!memory->words || !image_load(image, parallel_psram_image, memory))
        goto invalid;
    memcpy(memory->pins, pins, sizeof pins);
    s_cb_data cleanup = {.reason = cbEndOfSimulation, .cb_rtn = parallel_psram_free,
                         .user_data = (PLI_BYTE8 *)memory};
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback)
        goto invalid;
    vpi_free_object(callback);
    vpi_put_userdata(call, memory);
    memory_inspection_open(&memory->inspection);
    return memory;

invalid:
    free(memory->words);
    free(memory);
    return NULL;
}

/* EN: Commit the last sampled write to the lanes in mask of memory.
 * RU: Записывает последнюю выборку в байты memory, заданные mask. */
static void parallel_psram_commit(ParallelPsram *memory, unsigned mask)
{
    ParallelPsramWord *word = &memory->words[memory->write_address];
    ParallelPsramWord before = *word;
    word->data = (word->data & ~mask) | (memory->write_data.aval & mask);
    word->unknown = (word->unknown & ~mask) | (memory->write_data.bval & mask);
    word->written |= mask;
    if (memory->inspection.file)
    {
        unsigned before_unwritten = UINT16_MAX ^ before.written;
        unsigned after_unwritten = UINT16_MAX ^ word->written;
        MemoryChange change = {
            .address = memory->write_address,
            .before = before.data | before_unwritten,
            .before_unknown = before.unknown | before_unwritten,
            .after = word->data | after_unwritten,
            .after_unknown = word->unknown | after_unwritten
        };
        memory_inspection_record(&memory->inspection, change);
    }
}

/* EN: Sample asynchronous PSRAM pins on the current VPI call. user_data is unused.
 * Commit lanes on WE#, CE# or their byte enable rising. Disabled lanes stay in Z.
 * RU: Считывает выводы PSRAM текущего вызова VPI. user_data не используется.
 * Завершает запись по фронту WE#, CE# или разрешения байта. Отключённые байты в Z. */
static PLI_INT32 parallel_psram_update(PLI_BYTE8 *user_data)
{
    (void)user_data;
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    ParallelPsram *memory = vpi_get_userdata(call);
    if (!memory)
        memory = parallel_psram_open(call);
    if (!memory) {
        vpi_printf("Geany parallel PSRAM: invalid configuration or image.\n");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }

    vpiHandle *pins = memory->pins;
    int chip = scalar(pins[PSRAM_CE_N]), write = scalar(pins[PSRAM_WE_N]);
    s_vpi_vecval address = vector_word(pins[PSRAM_ADDRESS]);
    unsigned index = (unsigned)address.aval & (memory->size - 1);
    int known_address = !(address.bval & (memory->size - 1));
    int lower = scalar(pins[PSRAM_LB_N]), upper = scalar(pins[PSRAM_UB_N]);
    unsigned lanes = (lower == vpi0 ? 0x00ffu : 0) | (upper == vpi0 ? 0xff00u : 0);
    unsigned ended = chip == vpi1 || write == vpi1 ? UINT16_MAX :
                     (lower == vpi1 ? 0x00ffu : 0) | (upper == vpi1 ? 0xff00u : 0);
    if (memory->writing && (ended & memory->write_mask))
        parallel_psram_commit(memory, ended & memory->write_mask);

    memory->writing = chip == vpi0 && write == vpi0 && known_address;
    if (memory->writing) {
        memory->write_address = index;
        memory->write_mask = lanes;
        memory->write_data = vector_word(pins[PSRAM_DQ]);
    }

    s_vpi_vecval drive = {0, UINT16_MAX};
    if (chip == vpi0 && write == vpi1 && scalar(pins[PSRAM_OE_N]) == vpi0) {
        ParallelPsramWord *word = &memory->words[index];
        unsigned known_lanes = known_address ? lanes & word->written : 0;
        unsigned unwritten = lanes & ~known_lanes;
        drive.aval = (word->data & known_lanes) | unwritten;
        drive.bval = (word->unknown & known_lanes) | (UINT16_MAX & ~known_lanes);
    }
    if (!memory->drive_valid || drive.aval != memory->driven.aval ||
        drive.bval != memory->driven.bval) {
        memory->driven = drive;
        memory->drive_valid = 1;
        s_vpi_value value = {.format = vpiVectorVal};
        value.value.vector = &drive;
        vpi_put_value(pins[PSRAM_DRIVE], &value, NULL, vpiNoDelay);
    }
    return 0;
}
