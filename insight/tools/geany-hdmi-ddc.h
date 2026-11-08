/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Read-only HDMI DDC/EDID sink and simulated hot-plug output.
 * RU: Приёмник HDMI DDC/EDID только для чтения и сигнал горячего подключения.
 */

enum { DDC_SCL, DDC_SDA, DDC_PLUG, DDC_DRIVE, DDC_HPD, DDC_PORTS };
enum { EDID_BLOCK = 128, EDID_MAX = 256 };

typedef struct {
    I2cEeprom bus;
    vpiHandle pins[DDC_PORTS];
    unsigned char loaded[EDID_MAX];
    int connected;
} HdmiDdc;

static const uint8_t edid_header[] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};

/* EN: Fill bytes with the built-in EDID 1.3 base block for 640×480 at 60 Hz.
 * RU: Заполняет bytes встроенным базовым EDID 1.3 для 640×480 при 60 Гц. */
static void hdmi_edid_default(uint8_t *bytes)
{
    const uint8_t timing[] = {0xd6, 0x09, 0x80, 0xa0, 0x20, 0xe0, 0x2d, 0x10,
                              0x10, 0x60, 0xa2, 0x00, 0xa0, 0x78, 0x00, 0x00,
                              0x00, 0x18};
    const uint8_t chromaticity[] = {0xee, 0x91, 0xa3, 0x54, 0x4c, 0x99, 0x26, 0x0f, 0x50, 0x54};
    const uint8_t name[] = {0, 0, 0, 0xfc, 0, 'G', 'e', 'a', 'n', 'y', ' ',
                            'D', 'i', 's', 'p', 'l', 'a', 'y'};
    memset(bytes, 0, EDID_BLOCK);
    memcpy(bytes, edid_header, sizeof edid_header);
    bytes[8] = 0x1c;
    bytes[9] = 0xb9;
    bytes[10] = 1;
    bytes[16] = 1;
    bytes[17] = 36;
    bytes[18] = 1;
    bytes[19] = 3;
    bytes[20] = 0x80;
    bytes[21] = 16;
    bytes[22] = 12;
    bytes[23] = 120;
    bytes[24] = 0x06;
    memcpy(bytes + 25, chromaticity, sizeof chromaticity);
    bytes[35] = 0x20;
    memset(bytes + 38, 1, 16);
    memcpy(bytes + 54, timing, sizeof timing);
    memcpy(bytes + 72, name, sizeof name);
    bytes[93] = bytes[111] = 0x10;
    unsigned sum = 0;
    for (unsigned i = 0; i < EDID_BLOCK - 1; ++i)
        sum += bytes[i];
    bytes[EDID_BLOCK - 1] = (uint8_t)(0u - sum);
}

/* EN: Save image value at address in DDC state data, rejecting out-of-range data.
 * RU: Сохраняет value образа по address в DDC из data с проверкой границ. */
static int hdmi_edid_write(void *data, uint32_t address, uint64_t value)
{
    HdmiDdc *ddc = data;
    if (address >= EDID_MAX || value > UINT8_MAX)
        return 0;
    ddc->loaded[address] = 1;
    ddc->bus.bytes[address] = (uint8_t)value;
    return 1;
}

/* EN: Check ddc's custom image for the header, block count and every checksum.
 * RU: Проверяет заголовок, число блоков и контрольные суммы образа ddc. */
static int hdmi_edid_valid(const HdmiDdc *ddc)
{
    const uint8_t *bytes = ddc->bus.bytes;
    if (memcmp(bytes, edid_header, sizeof edid_header) || bytes[126] > 1)
        return 0;
    unsigned limit = EDID_BLOCK * (1u + bytes[126]);
    unsigned sum = 0;
    for (unsigned i = 0; i < EDID_MAX; ++i) {
        if (ddc->loaded[i] != (i < limit))
            return 0;
        if (i < limit) {
            sum = (sum + bytes[i]) & 255;
            if ((i + 1) % EDID_BLOCK == 0 && sum)
                return 0;
        }
    }
    return 1;
}

/* EN: Release the DDC state owned by callback at simulation end.
 * RU: Освобождает состояние DDC из callback при завершении симуляции. */
static PLI_INT32 hdmi_ddc_free(p_cb_data callback)
{
    free(callback->user_data);
    return 0;
}

/* EN: Bind call ports and load its EDID image. Return NULL on failure.
 * RU: Подключает порты call и загружает образ EDID. При ошибке возвращает NULL. */
static HdmiDdc *hdmi_ddc_open(vpiHandle call)
{
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    vpiHandle pins[DDC_PORTS];
    for (unsigned i = 0; i < DDC_PORTS; ++i) {
        pins[i] = vpi_scan(args);
        if (!pins[i])
            return NULL;
        if (vpi_get(vpiSize, pins[i]) != 1) {
            vpi_free_object(args);
            return NULL;
        }
    }
    vpiHandle image = vpi_scan(args);
    if (image && vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    HdmiDdc *ddc = calloc(1, sizeof *ddc);
    if (!ddc)
        return NULL;

    i2c_eeprom_init(&ddc->bus, 0x50);
    ddc->bus.read_only = 1;
    if (image) {
        if (!image_load(image, hdmi_edid_write, ddc) || !hdmi_edid_valid(ddc))
            goto invalid;
    } else {
        hdmi_edid_default(ddc->bus.bytes);
    }
    memcpy(ddc->pins, pins, sizeof pins);
    s_cb_data cleanup = {.reason = cbEndOfSimulation, .cb_rtn = hdmi_ddc_free,
                         .user_data = (PLI_BYTE8 *)ddc};
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback)
        goto invalid;
    vpi_free_object(callback);
    vpi_put_userdata(call, ddc);
    return ddc;

invalid:
    free(ddc);
    return NULL;
}

/* EN: Advance the current DDC call. user_data is unused. Drive HPD only after
 * validating the image and release SDA whenever the virtual cable is unplugged.
 * RU: Обрабатывает текущий вызов DDC. user_data не используется. Поднимает HPD
 * после проверки образа и отпускает SDA при отключении виртуального кабеля. */
static PLI_INT32 hdmi_ddc_update(PLI_BYTE8 *user_data)
{
    (void)user_data;
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    HdmiDdc *ddc = vpi_get_userdata(call);
    if (!ddc)
        ddc = hdmi_ddc_open(call);
    if (!ddc) {
        vpi_printf("Geany HDMI DDC: expected a valid 128- or 256-byte EDID image.\n");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }

    int clock = scalar(ddc->pins[DDC_SCL]), data = scalar(ddc->pins[DDC_SDA]);
    int connected = scalar(ddc->pins[DDC_PLUG]) == vpi1;
    int previous_drive = ddc->bus.pull_low;
    if (connected != ddc->connected) {
        ddc->connected = connected;
        ddc->bus.phase = I2C_IDLE;
        ddc->bus.stage = I2C_SELECT;
        ddc->bus.address = ddc->bus.bits = ddc->bus.input = 0;
        ddc->bus.pull_low = 0;
        ddc->bus.clock = clock == vpi1;
        ddc->bus.data = data == vpi1;
        bus_put(ddc->pins[DDC_HPD], connected);
    } else if (connected && (clock == vpi0 || clock == vpi1) &&
               (data == vpi0 || data == vpi1)) {
        i2c_eeprom_edge(&ddc->bus, clock == vpi1, data == vpi1);
    }

    if (previous_drive != ddc->bus.pull_low) {
        s_vpi_value value = {.format = vpiScalarVal};
        value.value.scalar = ddc->bus.pull_low ? vpi0 : vpiZ;
        vpi_put_value(ddc->pins[DDC_DRIVE], &value, NULL, vpiNoDelay);
    }
    return 0;
}
