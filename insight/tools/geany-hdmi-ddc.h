/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Read-only HDMI DDC/EDID sink and simulated hot-plug output.
 * RU: Приёмник HDMI DDC/EDID только для чтения и сигнал горячего подключения.
 */

enum { DDC_SCL, DDC_SDA, DDC_PLUG, DDC_DRIVE, DDC_HPD, DDC_WIDTH, DDC_HEIGHT, DDC_REFRESH, DDC_PORTS };
enum { EDID_BLOCK = 128, EDID_MAX = 256 };

typedef struct {
    I2cEeprom bus;
    vpiHandle pins[DDC_PORTS];
    unsigned char loaded[EDID_MAX];
    int connected;
    MemoryInspection inspection;
} HdmiDdc;

static const uint8_t edid_header[] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};

/* EN: Set the checksum of an EDID block at bytes.
 * RU: Записывает контрольную сумму блока EDID из bytes. */
static void hdmi_edid_checksum(uint8_t *bytes)
{
    unsigned sum = 0;
    for (unsigned i = 0; i < EDID_BLOCK - 1; ++i)
        sum += bytes[i];
    bytes[EDID_BLOCK - 1] = (uint8_t)(0u - sum);
}

/* EN: Encode width, height and refresh in Hz in the timing descriptor dtd.
 * RU: Кодирует width, height и частоту refresh в Гц в дескрипторе dtd. */
static void hdmi_edid_timing(uint8_t *dtd, unsigned width, unsigned height, unsigned refresh)
{
    unsigned hblank = 160, vblank = 45, hoffset = 48, hsync = 32, voffset = 3, vsync = 5;
    unsigned clock, flags = 0x1e;
    if (width == 640 && height == 480) {
        vblank = 45;
        hoffset = 16;
        hsync = 96;
        voffset = 10;
        vsync = 2;
        clock = refresh == 60 ? 2518 : 0;
        flags = 0x18;
    } else if (width == 1280 && height == 720) {
        hblank = 370;
        vblank = 30;
        hoffset = 110;
        hsync = 40;
        voffset = 5;
        clock = refresh == 60 ? 7425 : 0;
    } else if (width == 1920 && height == 1080) {
        hblank = 280;
        hoffset = 88;
        hsync = 44;
        voffset = 4;
        clock = refresh == 60 ? 14850 : 0;
    } else if (width == 3840 && height == 2160 && refresh == 30) {
        hblank = 560;
        vblank = 90;
        hoffset = 176;
        hsync = 88;
        voffset = 8;
        vsync = 10;
        clock = 29700;
    } else {
        clock = 0;
    }
    if (!clock)
        clock = ((width + hblank) * (height + vblank) * refresh + 5000) / 10000;
    dtd[0] = (uint8_t)clock;
    dtd[1] = (uint8_t)(clock >> 8);
    dtd[2] = (uint8_t)width;
    dtd[3] = (uint8_t)hblank;
    dtd[4] = (uint8_t)(((width >> 8) << 4) | (hblank >> 8));
    dtd[5] = (uint8_t)height;
    dtd[6] = (uint8_t)vblank;
    dtd[7] = (uint8_t)(((height >> 8) << 4) | (vblank >> 8));
    dtd[8] = (uint8_t)hoffset;
    dtd[9] = (uint8_t)hsync;
    dtd[10] = (uint8_t)((voffset << 4) | vsync);
    dtd[11] = (uint8_t)(((hoffset >> 8) << 6) | ((hsync >> 8) << 4));
    dtd[12] = 160;
    dtd[13] = (uint8_t)(160 * height / width > 255 ? 255 : 160 * height / width);
    dtd[17] = (uint8_t)flags;
}

/* EN: Fill bytes with an EDID for width by height and refresh in Hz.
 * RU: Заполняет bytes EDID для width на height и частоты refresh в Гц. */
static void hdmi_edid_default(uint8_t *bytes, unsigned width, unsigned height, unsigned refresh)
{
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
    if (width == 640 && height == 480 && refresh == 60)
        bytes[35] = 0x20;
    memset(bytes + 38, 1, 16);
    hdmi_edid_timing(bytes + 54, width, height, refresh);
    memcpy(bytes + 72, name, sizeof name);
    bytes[93] = bytes[111] = 0x10;
    if (width == 3840 && height == 2160) {
        /* CTA VIC 93, 94 or 95, and a 300 MHz HDMI vendor block. */
        uint8_t vic = refresh == 24 ? 93 : refresh == 25 ? 94 : 95;
        const uint8_t cta[] = {0x02, 0x03, 14, 0, 0x41, (uint8_t)(0x80 | vic),
                               0x67, 0x03, 0x0c, 0x00, 0x10, 0x00, 0x00, 60};
        memset(bytes + EDID_BLOCK, 0, EDID_BLOCK);
        memcpy(bytes + EDID_BLOCK, cta, sizeof cta);
        hdmi_edid_checksum(bytes + EDID_BLOCK);
        bytes[126] = 1;
    }
    hdmi_edid_checksum(bytes);
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

/* EN: Check the image in ddc and report a precise fault for filename.
 * Return nonzero only if the declared EDID blocks are complete and valid.
 * RU: Проверяет образ ddc и выводит точную причину ошибки для filename.
 * Возвращает ненулевое значение, если все заявленные блоки EDID корректны. */
static int hdmi_edid_valid(const HdmiDdc *ddc, const char *filename)
{
    const uint8_t *bytes = ddc->bus.bytes;
    if (!ddc->loaded[126]) {
        vpi_printf("Geany HDMI DDC: %s: incomplete EDID base block.\n", filename);
        return 0;
    }
    if (bytes[126] > 1) {
        vpi_printf("Geany HDMI DDC: %s: EDID declares %u extensions; at most one is supported.\n",
                   filename, (unsigned)bytes[126]);
        return 0;
    }

    unsigned limit = EDID_BLOCK * (1u + bytes[126]);
    for (unsigned i = 0; i < EDID_MAX; ++i) {
        if (ddc->loaded[i] != (i < limit)) {
            vpi_printf("Geany HDMI DDC: %s: %s EDID byte at offset 0x%02x.\n",
                       filename, i < limit ? "missing" : "unexpected", i);
            return 0;
        }
    }
    if (memcmp(bytes, edid_header, sizeof edid_header)) {
        vpi_printf("Geany HDMI DDC: %s: invalid EDID header.\n", filename);
        return 0;
    }
    for (unsigned offset = 0; offset < limit; offset += EDID_BLOCK) {
        unsigned sum = 0;
        for (unsigned i = 0; i < EDID_BLOCK; ++i)
            sum += bytes[offset + i];
        if (sum & 255) {
            vpi_printf("Geany HDMI DDC: %s: invalid EDID checksum in block %u.\n",
                       filename, offset / EDID_BLOCK);
            return 0;
        }
    }
    return 1;
}

/* EN: Export the read-only EDID and release the DDC state from callback.
 * RU: Сохраняет EDID только для чтения и освобождает состояние DDC из callback. */
static PLI_INT32 hdmi_ddc_free(p_cb_data callback)
{
    HdmiDdc *ddc = (HdmiDdc *)callback->user_data;
    unsigned size = ddc->bus.bytes[126] ? EDID_MAX : EDID_BLOCK;
    memory_inspection_export_bytes(&ddc->inspection, "EDID-ROM", ddc->bus.bytes, size, 0, 1);
    free(ddc);
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
        if (i <= DDC_HPD && vpi_get(vpiSize, pins[i]) != 1) {
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
        int loaded = image_load(image, hdmi_edid_write, ddc);
        s_vpi_value filename = {.format = vpiStringVal};
        vpi_get_value(image, &filename);
        if (!loaded) {
            vpi_printf("Geany HDMI DDC: %s: could not load the EDID HEX image.\n",
                       filename.value.str);
            goto invalid;
        }
        if (!hdmi_edid_valid(ddc, filename.value.str))
            goto invalid;
    } else {
        unsigned width = (unsigned)bus_integer(pins[DDC_WIDTH]);
        unsigned height = (unsigned)bus_integer(pins[DDC_HEIGHT]);
        if (!width || width > 3840 || !height || height > 2160)
            goto invalid;
        unsigned maximum = width <= 1920 && height <= 1080 ? 60 : 30;
        unsigned refresh = (unsigned)bus_integer(pins[DDC_REFRESH]);
        if (!refresh)
            refresh = maximum;
        if (refresh < 24 || refresh > maximum ||
            (width == 3840 && height == 2160 && refresh != 24 && refresh != 25 && refresh != 30))
            goto invalid;
        hdmi_edid_default(ddc->bus.bytes, width, height, refresh);
    }
    memcpy(ddc->pins, pins, sizeof pins);
    s_cb_data cleanup = {.reason = cbEndOfSimulation, .cb_rtn = hdmi_ddc_free,
                         .user_data = (PLI_BYTE8 *)ddc};
    vpiHandle callback = vpi_register_cb(&cleanup);
    if (!callback)
        goto invalid;
    vpi_free_object(callback);
    vpi_put_userdata(call, ddc);
    memory_inspection_open(&ddc->inspection);
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
        vpi_printf("Geany HDMI DDC: could not initialize the HDMI sink.\n");
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
