/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Clocked 8N1 USART peer for the Verilog debugger's serial console.
 * RU: Синхронное устройство USART 8N1 для консоли отладчика Verilog.
 */

enum {
    USART_CLOCK, USART_DUT_TX, USART_DUT_RX, USART_SEND_BYTE,
    USART_REQUEST, USART_ACK, USART_EDGE, USART_ARGUMENTS
};

typedef struct {
    vpiHandle ports[USART_ARGUMENTS];
    int last_clock, sample_edge;
    unsigned rx_phase, rx_byte;
    unsigned tx_phase, tx_byte, tx_request;
    int rx_valid;
} GeanyUsart;

/* EN: Free the USART state owned by the end-of-simulation callback.
 * RU: Освобождает состояние USART, принадлежащее callback завершения симуляции. */
static PLI_INT32 geany_usart_free(p_cb_data callback)
{
    free(callback->user_data);
    return 0;
}

/* EN: Read VPI arguments of call. Return new USART state or NULL for an invalid call.
 * RU: Читает аргументы VPI call. Возвращает состояние USART или NULL при ошибке. */
static GeanyUsart *geany_usart_open(vpiHandle call)
{
    vpiHandle ports[USART_ARGUMENTS];
    vpiHandle arguments = vpi_iterate(vpiArgument, call);
    if (!arguments)
        return NULL;
    for (unsigned i = 0; i < USART_ARGUMENTS; i++) {
        ports[i] = vpi_scan(arguments);
        if (!ports[i])
            return NULL;
    }
    if (vpi_scan(arguments)) {
        vpi_free_object(arguments);
        return NULL;
    }

    int edge = scalar(ports[USART_EDGE]);
    if (edge != vpi0 && edge != vpi1)
        return NULL;
    GeanyUsart *usart = calloc(1, sizeof *usart);
    if (!usart)
        return NULL;
    memcpy(usart->ports, ports, sizeof ports);
    usart->last_clock = vpiX;
    usart->sample_edge = edge;
    s_cb_data cleanup = {.reason = cbEndOfSimulation, .cb_rtn = geany_usart_free,
                         .user_data = (PLI_BYTE8 *)usart};
    vpi_register_cb(&cleanup);
    vpi_put_userdata(call, usart);
    return usart;
}

/* EN: Sample DUT TX for usart and publish complete, valid 8N1 frames.
 * RU: Считывает TX схемы для usart и выводит полные корректные кадры 8N1. */
static void geany_usart_receive(GeanyUsart *usart)
{
    int bit = scalar(usart->ports[USART_DUT_TX]);
    if (!usart->rx_phase) {
        if (bit == vpi0) {
            usart->rx_phase = 1;
            usart->rx_byte = 0;
            usart->rx_valid = 1;
        }
        return;
    }
    if (usart->rx_phase <= 8) {
        if (bit == vpi1)
            usart->rx_byte |= 1u << (usart->rx_phase - 1);
        else if (bit != vpi0)
            usart->rx_valid = 0;
        ++usart->rx_phase;
        return;
    }
    if (usart->rx_valid && bit == vpi1)
        vpi_printf("GEANY-UART\t%02x\n", usart->rx_byte);
    usart->rx_phase = 0;
}

/* EN: Drive the next bit for usart on the nonsampling edge. Acknowledge a
 * queued byte only after the DUT has had a sampling edge for its stop bit.
 * RU: Выставляет следующий бит usart на противоположном фронте. Подтверждает
 * байт после фронта, на котором схема считывает стоповый бит. */
static void geany_usart_transmit(GeanyUsart *usart)
{
    if (usart->tx_phase == 10) {
        bus_put(usart->ports[USART_ACK], usart->tx_request);
        usart->tx_phase = 0;
        return;
    }
    if (!usart->tx_phase) {
        unsigned request = (unsigned)bus_integer(usart->ports[USART_REQUEST]) & 1;
        if (request == usart->tx_request)
            return;
        usart->tx_request = request;
        usart->tx_byte = (unsigned)bus_integer(usart->ports[USART_SEND_BYTE]) & 255;
        bus_put(usart->ports[USART_DUT_RX], 0);
    } else {
        int bit = usart->tx_phase == 9 ? 1 : (usart->tx_byte >> (usart->tx_phase - 1)) & 1;
        bus_put(usart->ports[USART_DUT_RX], bit);
    }
    ++usart->tx_phase;
}

/* EN: Handle a known clock transition in the current VPI call. user_data is unused.
 * RU: Обрабатывает известный перепад такта текущего вызова VPI. user_data не используется. */
static PLI_INT32 geany_usart_tick(PLI_BYTE8 *user_data)
{
    (void)user_data;
    vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
    GeanyUsart *usart = vpi_get_userdata(call);
    if (!usart)
        usart = geany_usart_open(call);
    if (!usart) {
        vpi_printf("Geany USART: invalid VPI arguments.\n");
        vpip_set_return_value(1);
        vpi_control(vpiFinish, 1);
        return 0;
    }
    int previous = usart->last_clock;
    int clock = scalar(usart->ports[USART_CLOCK]);
    usart->last_clock = clock;
    if (clock == previous || (previous != vpi0 && previous != vpi1) ||
        (clock != vpi0 && clock != vpi1))
        return 0;
    if (clock == usart->sample_edge)
        geany_usart_receive(usart);
    else
        geany_usart_transmit(usart);
    return 0;
}
