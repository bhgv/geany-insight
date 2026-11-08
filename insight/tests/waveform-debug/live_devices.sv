// EN: Drives live instrument and preprocessed port-discovery checks in the GTK test.
// RU: Схема для проверки живых приборов и списка портов после препроцессора в GTK.
`timescale 1ns/1ns
`define GEANY_LIVE_HAS_SLIDER
`define GEANY_LIVE_SLIDER_PORT level
module live_devices(
    input wire enable, rx, switch_on, push,
    input wire [7:0] external_data,
`ifdef GEANY_LIVE_HAS_SLIDER
    input wire [15:0] `GEANY_LIVE_SLIDER_PORT,
`else
    input wire [7:0] inactive_slider,
`endif
    output wire [7:0] model_result, level_out,
    output wire [15:0] hex_out,
    output wire ready, tx,
    output reg pushed = 0,
    output reg [3:0] addr,
    inout wire [7:0] dq,
    output reg ce_n, we_n, oe_n,
    output reg sck = 0, cs_n = 1, mosi = 0,
    input wire miso,
    inout wire scl, sda,
    output reg [7:0] spi_result = 0, i2c_result = 0,
    output reg [7:0] result
);
    reg drive;
    assign ready = enable & switch_on;
    always @(posedge push) pushed <= 1;
    assign tx = rx;
    assign model_result = external_data;
    assign level_out = level[7:0];
    assign hex_out = {8'hbe, level_out};
    assign dq = drive ? 8'hA5 : 8'bz;
    initial begin
        result = 0;
        addr = 4'h3;
        ce_n = 0;
        we_n = 0;
        oe_n = 1;
        drive = 1;
        #3000;
        we_n = 1;
        drive = 0;
        oe_n = 0;
        #1000;
        result = dq;
    end

    // EN: Read an initialized byte from the SPI NOR selected in the wizard.
    // RU: Читаем начальный байт SPI NOR, выбранной в мастере.
    reg [7:0] spi_received;
    task spi_transfer(input [7:0] data);
        for (integer bit_index = 7; bit_index >= 0; bit_index--) begin
            mosi = data[bit_index];
            #1 sck = 1;
            #1 spi_received[bit_index] = miso;
            sck = 0;
        end
    endtask

    initial begin
        #20 cs_n = 0;
        #1 spi_transfer(8'h03);
        spi_transfer(0); spi_transfer(0); spi_transfer(8'h10);
        spi_transfer(0);
        spi_result = spi_received;
        cs_n = 1;
    end

    // EN: Address the I2C EEPROM and read its initialized word over open drain.
    // RU: Адресуем EEPROM I²C и читаем начальные данные через открытый сток.
    reg scl_low = 0, sda_low = 0;
    assign scl = scl_low ? 1'b0 : 1'bz;
    assign sda = sda_low ? 1'b0 : 1'bz;

    task i2c_start;
        sda_low = 0;
        #1 scl_low = 0;
        #1 sda_low = 1;
        #1 scl_low = 1;
    endtask

    task i2c_send(input [7:0] data);
        for (integer bit_index = 7; bit_index >= 0; bit_index--) begin
            sda_low = !data[bit_index];
            #1 scl_low = 0;
            #1 scl_low = 1;
            #1;
        end
        sda_low = 0;
        #1 scl_low = 0;
        #1 if (sda !== 1'b0) $fatal(1, "I2C EEPROM did not acknowledge %h", data);
        scl_low = 1;
        #1;
    endtask

    initial begin
        #20 i2c_start();
        i2c_send(8'ha0); i2c_send(8'h12); i2c_send(8'h34);
        i2c_start();
        i2c_send(8'ha1);
        for (integer bit_index = 7; bit_index >= 0; bit_index--) begin
            #1 scl_low = 0;
            #1 i2c_result[bit_index] = sda;
            scl_low = 1;
            #1;
        end
        // EN: End the read with NACK and STOP.
        // RU: Завершаем чтение сигналами NACK и STOP.
        #1 scl_low = 0;
        #1 scl_low = 1;
        #1 sda_low = 1;
        #1 scl_low = 0;
        #1 sda_low = 0;
    end
endmodule

`undef GEANY_LIVE_HAS_SLIDER
`undef GEANY_LIVE_SLIDER_PORT
