// EN: Exercise controller transfers, target replies, stretching and NACK.
// RU: Проверяет обмен ведущих, ответы ведомых, удержание SCL и отсутствие ACK.
`timescale 1ns/1ns
module bus_controllers_tb;
    reg spi_clk = 0, spi_mosi = 0, spi_cs = 1, spi_done = 0;
    reg nor_reply = 1'bz;
    always @(spi_clk or spi_cs)
        $geany_spi_nor(spi_clk, spi_cs, spi_mosi, nor_reply, "nor.hex");
    initial begin
        #10;
        while (!spi_done) begin
            $geany_spi_controller(spi_clk, nor_reply, spi_clk, spi_mosi,
                                  spi_cs, spi_done, "0300001000", 0, 0);
            #5;
        end
    end

    reg mode3_clk = 1, mode3_mosi = 0, mode3_cs = 1, mode3_done = 0;
    reg mode3_reply = 0;
    always @(negedge mode3_clk)
        if (!mode3_cs) mode3_reply <= mode3_mosi;
    initial begin
        #10;
        while (!mode3_done) begin
            $geany_spi_controller(mode3_clk, mode3_reply, mode3_clk, mode3_mosi,
                                  mode3_cs, mode3_done, "a6", 3, 0);
            #5;
        end
    end

    tri1 scl, sda;
    reg scl_release = 1, sda_release = 1, stretch = 0;
    reg i2c_dummy = 1, i2c_done = 0, eeprom_reply = 1'bz;
    assign scl = scl_release && !stretch ? 1'bz : 1'b0;
    assign sda = sda_release ? 1'bz : 1'b0;
    assign sda = eeprom_reply;
    always @(scl or sda)
        $geany_i2c_eeprom(scl, sda, 'h50, eeprom_reply, "i2c.hex");
    initial begin
        @(negedge scl);
        stretch = 1;
        #40 stretch = 0;
    end
    initial begin
        #10;
        while (!i2c_done) begin
            $geany_i2c_controller(scl, sda, scl_release, sda_release, i2c_dummy,
                                  i2c_done, "1234", 'h50, 2);
            #5;
        end
    end

    tri1 empty_scl, empty_sda;
    reg empty_clk = 1, empty_data = 1, empty_dummy = 1, empty_done = 0;
    assign empty_scl = empty_clk ? 1'bz : 1'b0;
    assign empty_sda = empty_data ? 1'bz : 1'b0;
    initial begin
        #10;
        while (!empty_done) begin
            $geany_i2c_controller(empty_scl, empty_sda, empty_clk, empty_data,
                                  empty_dummy, empty_done, "-", 'h53, 0);
            #5;
        end
    end

    initial begin
        #5000;
        if (!spi_done || !spi_cs || !mode3_done || !mode3_cs ||
            !i2c_done || !empty_done || {scl, sda, empty_scl, empty_sda} !== 4'b1111)
            $fatal(1, "Controller did not complete or release the bus");
        $display("Bus controllers test passed");
        $finish;
    end
endmodule
