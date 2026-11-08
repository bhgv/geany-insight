/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Assemble RGB, OpenLDI and TMDS pixels into bounded simulator frame snapshots.
 * RU: Собирает пиксели RGB, OpenLDI и TMDS в снимки кадров ограниченного размера.
 */

enum { RASTER_RGB, RASTER_JEIDA, RASTER_VESA, RASTER_HDMI, RASTER_DVI };
enum { RASTER_CK, RASTER_A, RASTER_B, RASTER_C, RASTER_D, RASTER_E,
       RASTER_WIDTH, RASTER_HEIGHT, RASTER_DEPTH, RASTER_VS, RASTER_MODE,
       RASTER_PHASE, RASTER_ID, RASTER_PORTS };
enum { RASTER_DUAL_LANES = 4, RASTER_DUAL_SWAP = RASTER_PORTS + RASTER_DUAL_LANES,
       RASTER_DUAL_PORTS };

typedef struct RasterSink RasterSink;
typedef struct {
    RasterSink *sink;
    uint32_t period, last_delay;
    unsigned bit, words[8];
    int active, bad;
} RasterSample;

struct RasterSink {
    vpiHandle pins[RASTER_DUAL_PORTS];
    unsigned width, height, depth, vs_active, mode, phase, id;
    unsigned x, y, vs, preamble, guard, video;
    int dual, swap;
    uint32_t sequence;
    uint64_t last_edge;
    int have_edge, active_line, discard, reported;
    uint8_t *rgb;
    RasterSample sample[3];
};

/* EN: Publish the next complete frame of sink through alternating snapshot files.
 * RU: Публикует полный кадр sink через два чередующихся файла снимков. */
static void raster_publish(RasterSink *sink)
{
    uint32_t next = sink->sequence + 1;
    if (!next)
        next = 1;
    char path[64], temporary[64];
    snprintf(path, sizeof path, "geany-raster-%u-%u.rgb", sink->id, (unsigned)(next & 1));
    snprintf(temporary, sizeof temporary, "geany-raster-%u.tmp", sink->id);
    FILE *file = fopen(temporary, "wb");
    if (file) {
        uint8_t header[] = {(uint8_t)next, (uint8_t)(next >> 8),
                            (uint8_t)(next >> 16), (uint8_t)(next >> 24)};
        size_t size = (size_t)sink->width * sink->height * 3;
        int ok = fwrite(header, 1, sizeof header, file) == sizeof header &&
                 fwrite(sink->rgb, 1, size, file) == size;
        if (fclose(file))
            ok = 0;
        if (ok) {
#ifdef _WIN32
            remove(path);
#endif
            if (!rename(temporary, path)) {
                sink->sequence = next;
                return;
            }
        }
        remove(temporary);
    }
    if (!sink->reported) {
        vpi_printf("Geany display %u: could not publish the frame snapshot.\n", sink->id);
        sink->reported = 1;
    }
}

/* EN: Consume DE, VS and an RGB pixel for sink. VS restarts raster alignment.
 * RU: Принимает DE, VS и пиксель RGB для sink. VS восстанавливает начало растра. */
static void raster_pixel(RasterSink *sink, unsigned de, unsigned vs,
                         unsigned red, unsigned green, unsigned blue)
{
    if (vs == sink->vs_active) {
        sink->x = sink->y = 0;
        sink->active_line = sink->discard = 0;
        return;
    }
    if (sink->discard || sink->y == sink->height)
        return;
    if (!de) {
        if (sink->active_line) {
            sink->active_line = 0;
            if (sink->x != sink->width)
                sink->discard = 1;
            else if (++sink->y == sink->height)
                raster_publish(sink);
            sink->x = 0;
        }
        return;
    }
    sink->active_line = 1;
    if (sink->x == sink->width) {
        sink->discard = 1;
        return;
    }

    size_t index = ((size_t)sink->y * sink->width + sink->x++) * 3;
    sink->rgb[index] = (uint8_t)red;
    sink->rgb[index + 1] = (uint8_t)green;
    sink->rgb[index + 2] = (uint8_t)blue;
}

/* EN: Expand a six-bit channel value to the full eight-bit range.
 * RU: Расширяет шестибитный канал value до полного восьмибитного диапазона. */
static unsigned raster_expand6(unsigned value)
{
    return (value << 2) | (value >> 4);
}

/* EN: Return the RGB value of words in the configured mapping of sink.
 * RU: Возвращает RGB из words по раскладке, выбранной в sink. */
static unsigned raster_lvds_color(RasterSink *sink, const unsigned *words)
{
    unsigned red = words[0] & 63u;
    unsigned green = ((words[1] & 31u) << 1) | (words[0] >> 6);
    unsigned blue = ((words[2] & 15u) << 2) | (words[1] >> 5);
    if (sink->depth == 6) {
        red = raster_expand6(red);
        green = raster_expand6(green);
        blue = raster_expand6(blue);
    } else if (sink->mode == RASTER_JEIDA) {
        red = (red << 2) | (words[3] & 3u);
        green = (green << 2) | ((words[3] >> 2) & 3u);
        blue = (blue << 2) | ((words[3] >> 4) & 3u);
    } else {
        red |= (words[3] & 3u) << 6;
        green |= ((words[3] >> 2) & 3u) << 6;
        blue |= ((words[3] >> 4) & 3u) << 6;
    }
    return (red << 16) | (green << 8) | blue;
}

/* EN: Append one pixel or an interleaved pair from OpenLDI lane words to sink.
 * RU: Добавляет в sink пиксель или чередующуюся пару из слов линий OpenLDI. */
static void raster_lvds(RasterSink *sink, const unsigned *words)
{
    unsigned de = (words[2] >> 6) & 1u, vs = (words[2] >> 5) & 1u;
    unsigned first = raster_lvds_color(sink, words + (sink->swap ? 4 : 0));
    raster_pixel(sink, de, vs, first >> 16, (first >> 8) & 255, first & 255);
    if (sink->dual && de && vs != sink->vs_active) {
        unsigned second = raster_lvds_color(sink, words + (sink->swap ? 0 : 4));
        raster_pixel(sink, de, vs, second >> 16, (second >> 8) & 255, second & 255);
    }
}

/* EN: Return the control value for TMDS word, or -1 for a noncontrol symbol.
 * RU: Возвращает значение управления для слова TMDS word, иначе -1. */
static int raster_tmds_control(unsigned word)
{
    switch (word) {
        case 0x354: return 0;
        case 0x0ab: return 1;
        case 0x154: return 2;
        case 0x2ab: return 3;
    }
    return -1;
}

/* EN: Recover eight video bits from a TMDS word, undoing inversion and XOR/XNOR.
 * RU: Восстанавливает восемь видеобитов из word TMDS, отменяя инверсию и XOR/XNOR. */
static unsigned raster_tmds_byte(unsigned word)
{
    unsigned encoded = (word ^ ((word & 0x200u) ? 255u : 0)) & 255u;
    unsigned decoded = ((encoded ^ (encoded << 1)) & 254u) ^ ((word & 0x100u) ? 0 : 254u);
    return decoded | (encoded & 1u);
}

/* EN: Decode the TMDS words for sink. HDMI mode gates video on its guard band;
 * DVI mode starts video after a control period.
 * RU: Декодирует words для sink. HDMI ждёт защитные символы видеоданных,
 * режим DVI начинает видео после управляющего периода. */
static void raster_hdmi(RasterSink *sink, const unsigned *words)
{
    int blue = raster_tmds_control(words[0]), green = raster_tmds_control(words[1]);
    int red = raster_tmds_control(words[2]);
    if (blue >= 0 && green >= 0 && red >= 0) {
        sink->vs = ((unsigned)blue >> 1) & 1u;
        sink->video = sink->mode == RASTER_DVI;
        sink->guard = 0;
        if (green == 1 && red == 0) {
            if (sink->preamble < 8)
                ++sink->preamble;
        } else {
            sink->preamble = 0;
        }
        raster_pixel(sink, 0, sink->vs, 0, 0, 0);
        return;
    }
    if (sink->mode == RASTER_HDMI && sink->preamble == 8 &&
        words[0] == 0x2cc && words[1] == 0x133 && words[2] == 0x2cc) {
        if (++sink->guard == 2) {
            sink->video = 1;
            sink->preamble = 0;
        }
        return;
    }
    sink->preamble = 0;
    if (!sink->video || blue >= 0 || green >= 0 || red >= 0)
        return;

    unsigned r = raster_tmds_byte(words[2]), g = raster_tmds_byte(words[1]);
    unsigned b = raster_tmds_byte(words[0]);
    if (sink->depth == 6) {
        r = raster_expand6(r >> 2);
        g = raster_expand6(g >> 2);
        b = raster_expand6(b >> 2);
    }
    raster_pixel(sink, 1, sink->vs, r, g, b);
}

static PLI_INT32 raster_sample_bit(p_cb_data callback);

/* EN: Schedule sample's next centered bit at the measured pixel period.
 * RU: Планирует следующую выборку sample в середине бита по измеренному периоду. */
static void raster_schedule(RasterSample *sample)
{
    RasterSink *sink = sample->sink;
    unsigned bits = sink->mode <= RASTER_VESA ? 7 : 10;
    uint32_t delay = (uint32_t)((uint64_t)sample->period *
                     (2u * (sample->bit + sink->phase) + 1) / (2u * bits));
    s_vpi_time time = {.type = vpiSimTime, .low = delay - sample->last_delay};
    sample->last_delay = delay;
    s_cb_data next = {.reason = cbAfterDelay, .cb_rtn = raster_sample_bit,
                      .time = &time, .user_data = (PLI_BYTE8 *)sample};
    vpiHandle event = vpi_register_cb(&next);
    if (event)
        vpi_free_object(event);
    else
        sample->active = 0;
}

/* EN: Sample logical lanes from callback and decode the completed serial word.
 * RU: Считывает логические линии по callback и декодирует завершённое слово. */
static PLI_INT32 raster_sample_bit(p_cb_data callback)
{
    RasterSample *sample = (RasterSample *)callback->user_data;
    RasterSink *sink = sample->sink;
    unsigned bits = sink->mode <= RASTER_VESA ? 7 : 10;
    unsigned lanes = sink->mode <= RASTER_VESA && sink->depth == 8 ? 4 : 3;
    for (unsigned i = 0; i < lanes; ++i) {
        int level = scalar(sink->pins[RASTER_A + i]);
        if (level == vpi1)
            sample->words[i] |= 1u << sample->bit;
        else if (level != vpi0)
            sample->bad = 1;
        if (sink->dual) {
            int second = scalar(sink->pins[RASTER_PORTS + i]);
            if (second == vpi1)
                sample->words[4 + i] |= 1u << sample->bit;
            else if (second != vpi0)
                sample->bad = 1;
        }
    }
    if (++sample->bit < bits) {
        raster_schedule(sample);
    } else {
        sample->active = 0;
        if (sample->bad)
            sink->discard = 1;
        else if (sink->mode <= RASTER_VESA)
            raster_lvds(sink, sample->words);
        else
            raster_hdmi(sink, sample->words);
    }
    return 0;
}

/* EN: Release the frame buffer and sink owned by callback at simulation end.
 * RU: Освобождает буфер кадров и приёмник из callback при завершении симуляции. */
static PLI_INT32 raster_free(p_cb_data callback)
{
    RasterSink *sink = (RasterSink *)callback->user_data;
    free(sink->rgb);
    free(sink);
    return 0;
}

/* EN: Validate ports of call and return a new sink. dual enables the second OpenLDI link.
 * RU: Проверяет выводы call и создаёт приёмник. dual включает второй канал OpenLDI. */
static RasterSink *raster_open(vpiHandle call, int dual)
{
    vpiHandle args = vpi_iterate(vpiArgument, call);
    if (!args)
        return NULL;
    vpiHandle pins[RASTER_DUAL_PORTS] = {0};
    unsigned count = dual ? RASTER_DUAL_PORTS : RASTER_PORTS;
    for (unsigned i = 0; i < count; ++i) {
        pins[i] = vpi_scan(args);
        if (!pins[i])
            return NULL;
    }
    if (vpi_scan(args)) {
        vpi_free_object(args);
        return NULL;
    }
    unsigned width = (unsigned)bus_integer(pins[RASTER_WIDTH]);
    unsigned height = (unsigned)bus_integer(pins[RASTER_HEIGHT]);
    unsigned depth = (unsigned)bus_integer(pins[RASTER_DEPTH]);
    unsigned mode = (unsigned)bus_integer(pins[RASTER_MODE]);
    unsigned phase = (unsigned)bus_integer(pins[RASTER_PHASE]);
    unsigned vs = (unsigned)bus_integer(pins[RASTER_VS]);
    unsigned id = (unsigned)bus_integer(pins[RASTER_ID]);
    if (!width || width > 3840 || !height || height > 2160 ||
        (dual && ((width & 1) || (mode != RASTER_JEIDA && mode != RASTER_VESA))) ||
        (depth != 6 && depth != 8) || mode > RASTER_DVI || vs > 1 || id >= 128 ||
        phase >= (mode <= RASTER_VESA ? 7u : 10u))
        return NULL;

    unsigned channels = mode == RASTER_RGB ? 5 : mode <= RASTER_VESA && depth == 8 ? 4 : 3;
    if (vpi_get(vpiSize, pins[RASTER_CK]) != 1)
        return NULL;
    for (unsigned i = 0; i < channels; ++i) {
        unsigned expected = mode == RASTER_RGB && i >= 2 ? depth : 1;
        if ((unsigned)vpi_get(vpiSize, pins[RASTER_A + i]) != expected)
            return NULL;
    }

    if (dual)
        for (unsigned i = 0; i < 4; ++i)
            if (vpi_get(vpiSize, pins[RASTER_PORTS + i]) != 1)
                return NULL;
    RasterSink *sink = calloc(1, sizeof *sink);
    if (!sink)
        return NULL;
    sink->rgb = malloc((size_t)width * height * 3);
    if (!sink->rgb) {
        free(sink);
        return NULL;
    }
    memcpy(sink->pins, pins, sizeof pins);
    sink->width = width;
    sink->height = height;
    sink->depth = depth;
    sink->mode = mode;
    sink->vs_active = vs;
    sink->vs = !vs;
    sink->phase = phase;
    sink->dual = dual;
    sink->swap = dual && bus_integer(pins[RASTER_DUAL_SWAP]) != 0;
    sink->id = id;
    s_cb_data cleanup = {.reason = cbEndOfSimulation, .cb_rtn = raster_free,
                         .user_data = (PLI_BYTE8 *)sink};
    vpiHandle event = vpi_register_cb(&cleanup);
    if (!event) {
        raster_free(&cleanup);
        return NULL;
    }
    vpi_free_object(event);
    vpi_put_userdata(call, sink);
    return sink;
}

/* EN: Accept a pixel-clock edge in the current VPI call. user_data selects dual OpenLDI.
 * RU: Принимает фронт пиксельного такта в текущем VPI-вызове. user_data выбирает два канала OpenLDI. */
static PLI_INT32 raster_tick(PLI_BYTE8 *user_data)
{
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    RasterSink *sink = vpi_get_userdata(call);
    if (!sink)
        sink = raster_open(call, user_data != NULL);
    if (!sink) {
        vpi_printf("Geany display: invalid display configuration.\n");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }
    if (sink->mode == RASTER_RGB) {
        s_vpi_vecval red = vector_word(sink->pins[RASTER_C]);
        s_vpi_vecval green = vector_word(sink->pins[RASTER_D]);
        s_vpi_vecval blue = vector_word(sink->pins[RASTER_E]);
        unsigned mask = (1u << sink->depth) - 1;
        unsigned r = (unsigned)red.aval & ~(unsigned)red.bval & mask;
        unsigned g = (unsigned)green.aval & ~(unsigned)green.bval & mask;
        unsigned b = (unsigned)blue.aval & ~(unsigned)blue.bval & mask;
        raster_pixel(sink, scalar(sink->pins[RASTER_A]) == vpi1,
                     scalar(sink->pins[RASTER_B]) == vpi1,
                     sink->depth == 6 ? raster_expand6(r) : r,
                     sink->depth == 6 ? raster_expand6(g) : g,
                     sink->depth == 6 ? raster_expand6(b) : b);
        return 0;
    }

    s_vpi_time time = {.type = vpiSimTime};
    vpi_get_time(NULL, &time);
    uint64_t tick = ((uint64_t)time.high << 32) | time.low;
    uint64_t period = tick - sink->last_edge;
    sink->last_edge = tick;
    if (!sink->have_edge) {
        sink->have_edge = 1;
        return 0;
    }
    if (period < (sink->mode <= RASTER_VESA ? 14u : 20u) || period > UINT32_MAX / 2) {
        if (!sink->reported) {
            vpi_printf("Geany display %u: serial pixel period is outside the supported tick range.\n",
                       sink->id);
            sink->reported = 1;
        }
        return 0;
    }
    for (unsigned i = 0; i < sizeof sink->sample / sizeof sink->sample[0]; ++i) {
        RasterSample *sample = &sink->sample[i];
        if (!sample->active) {
            *sample = (RasterSample){.sink = sink, .period = (uint32_t)period, .active = 1};
            raster_schedule(sample);
            break;
        }
    }
    return 0;
}
