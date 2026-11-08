/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Functional DDR5 x8 model with a two-cycle CA decoder and BL16 transfers.
 * RU: Функциональная DDR5 x8 с двухтактным декодером CA и передачами BL16.
 */

enum {
    DDR5_CK, DDR5_RESET, DDR5_CS, DDR5_CA, DDR5_DQ, DDR5_DQS, DDR5_DM,
    DDR5_RL, DDR5_WL, DDR5_DRIVE, DDR5_STROBE, DDR5_PORTS
};
enum { DDR5_ACT = 0, DDR5_WR = 0x0d, DDR5_RD = 0x1d, DDR5_PRE_ALL = 0x0b, DDR5_PRE = 0x1b };
enum { DDR5_BANKS = 32, DDR5_BURST = 16 };

typedef struct {
    vpiHandle pins[DDR5_PORTS];
    Ddr4 storage;
    uint16_t rows[DDR5_BANKS];
    uint32_t open;
    uint64_t cycle, due;
    unsigned first_ca, pending, mode, bank, column, beat, auto_close;
    unsigned read_latency, write_latency;
    int last_ck, last_dqs, driven_dqs, drive_valid, overflow;
    s_vpi_vecval driven_dq;
} GeanyDdr5;

/* EN: Write the image byte value at the packed address in DDR5 state data.
 * RU: Записывает байт образа value по упакованному address в DDR5 из data. */
static int ddr5_image(void *data, uint32_t address, uint64_t value)
{
    GeanyDdr5 *device = data;
    if (address > INT32_MAX || value > UINT8_MAX)
        return 0;
    Ddr4Cell *cell = ddr4_cell_key(&device->storage, address, 1);
    if (!cell)
        return 0;
    cell->data = (uint8_t)value;
    cell->unknown = 0;
    return 1;
}

/* EN: Free the device owned by the end-of-simulation callback.
 * RU: Освобождает устройство, принадлежащее callback завершения симуляции. */
static PLI_INT32 ddr5_free(p_cb_data callback)
{
    free(callback->user_data);
    return 0;
}

/* EN: Validate VPI call ports, load its optional image and return DDR5 state.
 * RU: Проверяет порты VPI call, загружает образ и возвращает состояние DDR5. */
static GeanyDdr5 *ddr5_open(vpiHandle call)
{
    vpiHandle pins[DDR5_PORTS];
    const int widths[] = {1, 1, 1, 14, 8, 1, 1, 0, 0, 8, 1};
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    for (unsigned i = 0; i < DDR5_PORTS; ++i) {
        pins[i] = vpi_scan(args);
        if (!pins[i])
            return NULL;
        if (widths[i] && vpi_get(vpiSize, pins[i]) != widths[i]) {
            vpi_free_object(args);
            return NULL;
        }
    }
    vpiHandle image = vpi_scan(args);
    if (image && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    s_vpi_vecval rl = vector_word(pins[DDR5_RL]), wl = vector_word(pins[DDR5_WL]);
    if (rl.bval || wl.bval || rl.aval < 1 || wl.aval < 1 || rl.aval > 64 || wl.aval > 64)
        return NULL;

    GeanyDdr5 *device = calloc(1, sizeof *device);
    if (!device)
        return NULL;
    if (!image_load(image, ddr5_image, device))
        goto invalid;
    memcpy(device->pins, pins, sizeof pins);
    device->read_latency = (unsigned)rl.aval;
    device->write_latency = (unsigned)wl.aval;
    device->last_ck = device->last_dqs = vpiX;
    s_cb_data cleanup = {.reason = cbEndOfSimulation, .cb_rtn = ddr5_free,
                         .user_data = (PLI_BYTE8 *)device};
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback)
        goto invalid;
    vpi_free_object(callback);
    vpi_put_userdata(call, device);
    return device;

invalid:
    free(device);
    return NULL;
}

/* EN: Process one CA word ca with chip select cs on a rising CK of device.
 * RU: Обрабатывает слово CA ca и выбор кристалла cs на фронте CK устройства device. */
static void ddr5_command(GeanyDdr5 *device, int cs, s_vpi_vecval ca)
{
    unsigned bits = (unsigned)ca.aval;
    unsigned unknown = (unsigned)ca.bval;
    if (device->pending) {
        unsigned first = device->first_ca;
        unsigned command = (first & 3u) ? first & 31u : DDR5_ACT;
        unsigned bank = (first >> 6) & 31u;
        uint32_t mask = UINT32_C(1) << bank;
        device->pending = 0;
        if (cs != vpi1)
            return;
        if (command == DDR5_ACT) {
            if (!(unknown & 0x0fffu) && !(device->open & mask)) {
                device->rows[bank] = (uint16_t)(((bits & 0x0fffu) << 4) | ((first >> 2) & 15u));
                device->open |= mask;
            }
        } else if (!device->mode && (device->open & mask)) {
            unsigned column_mask = command == DDR5_RD ? 0xffu : 0xfcu;
            if (unknown & (column_mask | (1u << 10)))
                return;
            device->mode = command == DDR5_RD ? DDR4_READ : DDR4_WRITE;
            device->bank = bank;
            device->column = (bits & column_mask) << 2;
            device->auto_close = !(bits & (1u << 10));
            device->beat = 0;
            device->due = device->cycle + (command == DDR5_RD ?
                          device->read_latency : device->write_latency);
        }
        return;
    }
    if (cs != vpi0 || (unknown & 3u))
        return;
    unsigned command = bits & 31u;
    if (!(bits & 3u) || command == DDR5_RD || command == DDR5_WR) {
        unsigned required = (bits & 3u) ? 0x7dfu : 0x7ffu;
        if (!(unknown & required)) {
            device->first_ca = bits;
            device->pending = 1;
        }
        return;
    }
    if ((unknown & 31u) || device->mode)
        return;
    unsigned bank = (bits >> 6) & 31u;
    if (command == DDR5_PRE && !(unknown & 0x7c0u))
        device->open &= ~(UINT32_C(1) << bank);
    else if (command == DDR5_PRE_ALL && !(unknown & (1u << 10))) {
        if (bits & (1u << 10)) {
            if (!(unknown & 0xc0u))
                device->open &= ~(UINT32_C(0x11111111) << (bank & 3u));
        } else {
            device->open = 0;
        }
    }
}

/* EN: Return the burst cell of device. create allocates a missing cell.
 * RU: Возвращает ячейку текущего пакета device. create разрешает её создать. */
static Ddr4Cell *ddr5_cell(GeanyDdr5 *device, int create)
{
    unsigned mask = DDR5_BURST - 1;
    /* BL16 reads reorder groups of four by C3:C2. Writes stay sequential. */
    unsigned column = (device->column & ~mask) | ((device->column ^ device->beat) & mask);
    uint32_t address = ((uint32_t)device->rows[device->bank] << 15) |
                       (device->bank << 10) | column;
    return ddr4_cell_key(&device->storage, address, create);
}

/* EN: Complete a beat of device and close its bank when auto-precharge was requested.
 * RU: Завершает передачу device и закрывает банк при запрошенном автопредзаряде. */
static void ddr5_advance(GeanyDdr5 *device)
{
    if (++device->beat == DDR5_BURST) {
        if (device->auto_close)
            device->open &= ~(UINT32_C(1) << device->bank);
        device->mode = DDR4_IDLE;
    }
}

/* EN: Drive data and strobe into device's adapter, skipping unchanged outputs.
 * RU: Выдаёт data и strobe в адаптер device, пропуская неизменившиеся значения. */
static void ddr5_drive(GeanyDdr5 *device, s_vpi_vecval data, int strobe)
{
    if (!device->drive_valid || data.aval != device->driven_dq.aval ||
        data.bval != device->driven_dq.bval) {
        s_vpi_value value = {.format = vpiVectorVal};
        value.value.vector = &data;
        vpi_put_value(device->pins[DDR5_DRIVE], &value, NULL, vpiNoDelay);
    }
    if (!device->drive_valid || strobe != device->driven_dqs) {
        s_vpi_value value = {.format = vpiScalarVal};
        value.value.scalar = strobe;
        vpi_put_value(device->pins[DDR5_STROBE], &value, NULL, vpiNoDelay);
    }
    device->drive_valid = 1;
    device->driven_dq = data;
    device->driven_dqs = strobe;
}

/* EN: Update DDR5 from the current VPI call. user_data is unused.
 * RU: Обновляет DDR5 по текущему вызову VPI. user_data не используется. */
static PLI_INT32 ddr5_update(PLI_BYTE8 *user_data)
{
    (void)user_data;
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    GeanyDdr5 *device = vpi_get_userdata(call);
    if (!device)
        device = ddr5_open(call);
    if (!device) {
        vpi_printf("Geany DDR5: invalid x8 port configuration or image.\n");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }
    int ck = scalar(device->pins[DDR5_CK]), dqs = scalar(device->pins[DDR5_DQS]);
    int ck_edge = ck != device->last_ck && (ck == vpi0 || ck == vpi1);
    int dqs_edge = dqs != device->last_dqs && (dqs == vpi0 || dqs == vpi1);
    device->last_ck = ck;
    device->last_dqs = dqs;
    if (scalar(device->pins[DDR5_RESET]) != vpi1) {
        device->cycle = device->open = device->pending = device->mode = 0;
        ddr5_drive(device, (s_vpi_vecval){0, 255}, vpiZ);
        return 0;
    }
    if (ck_edge && ck == vpi1) {
        ++device->cycle;
        ddr5_command(device, scalar(device->pins[DDR5_CS]), vector_word(device->pins[DDR5_CA]));
    }
    if (ck_edge && device->mode == DDR4_READ && device->cycle >= device->due) {
        Ddr4Cell *cell = ddr5_cell(device, 0);
        s_vpi_vecval data = cell ? (s_vpi_vecval){cell->data, cell->unknown} :
                                  (s_vpi_vecval){255, 255};
        ddr5_drive(device, data, ck);
        ddr5_advance(device);
    } else if (ck_edge) {
        ddr5_drive(device, (s_vpi_vecval){0, 255}, vpiZ);
    }
    if (dqs_edge && device->mode == DDR4_WRITE && device->cycle >= device->due) {
        if (scalar(device->pins[DDR5_DM]) == vpi1) {
            Ddr4Cell *cell = ddr5_cell(device, 1);
            if (cell) {
                s_vpi_vecval data = vector_word(device->pins[DDR5_DQ]);
                cell->data = (uint8_t)data.aval;
                cell->unknown = (uint8_t)data.bval;
            } else if (!device->overflow) {
                vpi_printf("Geany DDR5: functional storage limit reached.\n");
                device->overflow = 1;
            }
        }
        ddr5_advance(device);
    }
    return 0;
}
