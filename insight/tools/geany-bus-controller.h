/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Execute bounded SPI and I²C controller transactions from the VPI harness.
 * RU: Выполняет ограниченные транзакции ведущих SPI и I²C из тестбенча VPI. */

enum { BUS_MAX_BYTES = 64, BUS_MAX_STEPS = 4096, BUS_STRETCH_LIMIT = 1024 };
enum { BUS_DRIVE, BUS_SAMPLE, BUS_ACK };
enum { BUS_CLOCK_IN, BUS_DATA_IN, BUS_CLOCK_OUT, BUS_DATA_OUT, BUS_SELECT,
       BUS_DONE, BUS_PAYLOAD, BUS_ADDRESS, BUS_READ_COUNT, BUS_ARGUMENTS };

typedef struct {
    unsigned char action, clock, data, select;
} BusStep;

typedef struct {
    vpiHandle port[BUS_ARGUMENTS];
    BusStep steps[BUS_MAX_STEPS];
    unsigned length, position, stop, waits;
    unsigned octet, unknown, bits, received;
    int i2c;
} BusController;

/* EN: Append a timed drive or sample to bus. clock/data/select are logical levels.
 * RU: Добавляет действие в bus. clock/data/select задают логические уровни. */
static void bus_step(BusController *bus, unsigned action, unsigned clock,
                     unsigned data, unsigned select)
{
    if (bus->length < BUS_MAX_STEPS)
        bus->steps[bus->length++] = (BusStep){action, clock, data, select};
}

/* EN: Schedule one I²C bit on bus. Release SDA when data is 1 and perform action high.
 * RU: Планирует бит I²C на bus. data=1 отпускает SDA, action выполняется при высоком SCL. */
static void bus_i2c_bit(BusController *bus, unsigned data, unsigned action)
{
    bus_step(bus, BUS_DRIVE, 0, data, 0);
    bus_step(bus, BUS_DRIVE, 1, data, 0);
    bus_step(bus, action, 1, data, 0);
}

/* EN: Append an I²C write of byte to bus, including the target ACK.
 * RU: Добавляет запись byte на bus и считывание подтверждения ведомого. */
static void bus_i2c_write(BusController *bus, unsigned byte)
{
    for (unsigned bit = 8; bit > 0; --bit)
        bus_i2c_bit(bus, (byte >> (bit - 1)) & 1, BUS_DRIVE);
    bus_i2c_bit(bus, 1, BUS_ACK);
}

/* EN: Build a single transaction on bus. bytes holds length TX bytes. I²C uses
 * address and reads count bytes, with a repeated START after a nonempty write.
 * For SPI, address carries the mode (0–3) and count is unused.
 * RU: Строит транзакцию на bus из length байт bytes. I²C читает count байт
 * по адресу address, после непустой записи выполняет повторный START.
 * Для SPI address задаёт режим (0–3), count не используется. */
static void bus_plan(BusController *bus, const uint8_t *bytes, unsigned length,
                     unsigned address, unsigned count)
{
    if (!bus->i2c) {
        unsigned idle = address >> 1;
        unsigned trailing_sample = address & 1;
        bus_step(bus, BUS_DRIVE, idle, 0, 0);
        for (unsigned i = 0; i < length; ++i) {
            for (unsigned bit = 8; bit > 0; --bit) {
                unsigned data = (bytes[i] >> (bit - 1)) & 1;
                if (!trailing_sample) {
                    bus_step(bus, BUS_DRIVE, idle, data, 0);
                    bus_step(bus, BUS_DRIVE, idle, data, 0);
                }
                bus_step(bus, BUS_DRIVE, !idle, data, 0);
                if (trailing_sample) {
                    bus_step(bus, BUS_DRIVE, !idle, data, 0);
                    bus_step(bus, BUS_DRIVE, idle, data, 0);
                }
                bus_step(bus, BUS_SAMPLE, trailing_sample ? idle : !idle, data, 0);
            }
        }
        bus_step(bus, BUS_DRIVE, idle, 0, 0);
        bus_step(bus, BUS_DRIVE, idle, 0, 1);
        return;
    }

    bus_step(bus, BUS_DRIVE, 1, 1, 0);
    bus_step(bus, BUS_DRIVE, 1, 0, 0);
    if (length || !count) {
        bus_i2c_write(bus, address << 1);
        for (unsigned i = 0; i < length; ++i)
            bus_i2c_write(bus, bytes[i]);
    }
    if (count) {
        if (length) {
            bus_i2c_bit(bus, 1, BUS_DRIVE);
            bus_step(bus, BUS_DRIVE, 1, 0, 0);
        }
        bus_i2c_write(bus, (address << 1) | 1);
        for (unsigned i = 0; i < count; ++i) {
            for (unsigned bit = 0; bit < 8; ++bit)
                bus_i2c_bit(bus, 1, BUS_SAMPLE);
            bus_i2c_bit(bus, i + 1 == count, BUS_DRIVE);
        }
    }
    bus->stop = bus->length;
    bus_i2c_bit(bus, 0, BUS_DRIVE);
    bus_step(bus, BUS_DRIVE, 1, 1, 0);
}

/* EN: Free the controller stored in callback at simulation shutdown.
 * RU: Освобождает контроллер из callback при завершении симуляции. */
static PLI_INT32 bus_controller_free(p_cb_data callback)
{
    free(callback->user_data);
    return 0;
}

/* EN: Read integer VPI port and return its current value.
 * RU: Возвращает текущее целочисленное значение порта port. */
static int bus_integer(vpiHandle port)
{
    s_vpi_value value = {.format = vpiIntVal};
    vpi_get_value(port, &value);
    return value.value.integer;
}

/* EN: Deposit a scalar value in a harness register port.
 * RU: Записывает скаляр value в регистр port тестбенча. */
static void bus_put(vpiHandle port, int value)
{
    s_vpi_value scalar = {.format = vpiScalarVal};
    scalar.value.scalar = value ? vpi1 : vpi0;
    vpi_put_value(port, &scalar, NULL, vpiNoDelay);
}

/* EN: Decode a hexadecimal digit, returning -1 for any other character.
 * RU: Преобразует шестнадцатеричный символ digit, иначе возвращает -1. */
static int bus_hex(unsigned char digit)
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    if (digit >= 'A' && digit <= 'F')
        return digit - 'A' + 10;
    if (digit >= 'a' && digit <= 'f')
        return digit - 'a' + 10;
    return -1;
}

/* EN: Read and validate arguments of call. Return the new controller or NULL.
 * i2c selects the bus protocol. The end-of-simulation callback owns the state.
 * RU: Читает аргументы call и возвращает контроллер либо NULL. i2c выбирает
 * протокол. Состояние освобождается по завершении симуляции. */
static BusController *bus_controller_open(vpiHandle call, int i2c)
{
    BusController *bus = calloc(1, sizeof *bus);
    if (!bus)
        return NULL;

    vpiHandle args = vpi_iterate(vpiArgument, call);
    unsigned found = 0;
    while (args && found < BUS_ARGUMENTS) {
        vpiHandle port = vpi_scan(args);
        if (!port) {
            args = NULL;
            break;
        }
        bus->port[found++] = port;
    }
    if (args) {
        vpiHandle extra = vpi_scan(args);
        if (extra) {
            vpi_free_object(args);
            goto invalid;
        }
    }
    if (found != BUS_ARGUMENTS)
        goto invalid;

    int address = bus_integer(bus->port[BUS_ADDRESS]);
    int count = bus_integer(bus->port[BUS_READ_COUNT]);
    s_vpi_value value = {.format = vpiStringVal};
    vpi_get_value(bus->port[BUS_PAYLOAD], &value);
    const char *payload = value.value.str;
    size_t size = payload && strcmp(payload, "-") ? strlen(payload) : 0;
    if ((size % 2) || size > 2 * BUS_MAX_BYTES || (!i2c && !size) ||
        count < 0 || count > BUS_MAX_BYTES ||
        (i2c ? address < 8 || address > 119 : address < 0 || address > 3))
        goto invalid;
    uint8_t bytes[BUS_MAX_BYTES];
    for (size_t i = 0; i < size / 2; ++i) {
        int high = bus_hex(payload[2 * i]), low = bus_hex(payload[2 * i + 1]);
        if (high < 0 || low < 0)
            goto invalid;
        bytes[i] = (uint8_t)((high << 4) | low);
    }
    bus->i2c = i2c;
    bus_plan(bus, bytes, (unsigned)(size / 2), (unsigned)address, (unsigned)count);
    s_cb_data cleanup = {.reason = cbEndOfSimulation, .cb_rtn = bus_controller_free,
                         .user_data = (PLI_BYTE8 *)bus};
    vpi_register_cb(&cleanup);
    vpi_put_userdata(call, bus);
    return bus;

invalid:
    free(bus);
    return NULL;
}

/* EN: Advance the VPI call once per harness phase. user_data selects I²C.
 * Report received bytes and bus errors in the simulation console.
 * RU: Выполняет один шаг вызова VPI. user_data выбирает I²C.
 * Выводит полученные байты и ошибки шины в консоль симуляции. */
static PLI_INT32 bus_controller_tick(PLI_BYTE8 *user_data)
{
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    BusController *bus = vpi_get_userdata(call);
    if (!bus)
        bus = bus_controller_open(call, user_data != NULL);
    if (!bus) {
        vpi_printf("Geany bus controller: invalid transaction arguments.\n");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }
    if (bus->position == bus->length) {
        bus_put(bus->port[BUS_DONE], 1);
        return 0;
    }
    BusStep step = bus->steps[bus->position];
    /* A second consecutive high phase checks the settled, possibly stretched SCL. */
    if (bus->i2c && step.clock && bus->position &&
        bus->steps[bus->position - 1].clock &&
        bus->steps[bus->position - 1].action == BUS_DRIVE) {
        s_vpi_vecval clock = vector_word(bus->port[BUS_CLOCK_IN]);
        if ((clock.bval & 1) || !(clock.aval & 1)) {
            if (++bus->waits < BUS_STRETCH_LIMIT)
                return 0;
            vpi_printf("Geany I2C controller: SCL held low or unknown (timeout).\n");
            bus_put(bus->port[BUS_CLOCK_OUT], 1);
            bus_put(bus->port[BUS_DATA_OUT], 1);
            bus_put(bus->port[BUS_DONE], 1);
            return 0;
        }
    }
    bus->waits = 0;
    ++bus->position;

    if (step.action != BUS_DRIVE) {
        s_vpi_vecval data = vector_word(bus->port[BUS_DATA_IN]);
        if (step.action == BUS_ACK && ((data.aval | data.bval) & 1)) {
            vpi_printf("Geany I2C controller: %s, issuing STOP.\n",
                       data.bval & 1 ? "unknown ACK" : "NACK");
            bus->position = bus->stop;
        } else if (step.action == BUS_SAMPLE) {
            bus->octet = (bus->octet << 1) | (data.aval & 1);
            bus->unknown = (bus->unknown << 1) | (data.bval & 1);
            if (++bus->bits == 8) {
                const char *protocol = bus->i2c ? "I2C" : "SPI";
                if (bus->unknown)
                    vpi_printf("Geany %s controller: RX[%u] unknown (mask %02x)\n",
                               protocol, bus->received, bus->unknown);
                else
                    vpi_printf("Geany %s controller: RX[%u] = %02x\n",
                               protocol, bus->received, bus->octet);
                ++bus->received;
                bus->octet = bus->unknown = bus->bits = 0;
            }
        }
    }
    bus_put(bus->port[BUS_CLOCK_OUT], step.clock);
    bus_put(bus->port[BUS_DATA_OUT], step.data);
    bus_put(bus->port[BUS_SELECT], step.select);
    return 0;
}
