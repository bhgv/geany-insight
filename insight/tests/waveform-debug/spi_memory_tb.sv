`timescale 1ns/1ns
module spi_memory_tb;
    reg clk = 0, mosi = 0;
    reg [4:0] cs_n = '1;
    reg nor_out = 1'bz, nand_out = 1'bz, eeprom_out = 1'bz;
    reg psram_out = 1'bz, lua_out = 1'bz;
    wire miso = nor_out;
    assign miso = nand_out;
    assign miso = eeprom_out;
    assign miso = psram_out;
    assign miso = lua_out;

    always @(clk or cs_n[0]) $geany_spi_nor(clk, cs_n[0], mosi, nor_out, "nor.hex");
    always @(clk or cs_n[1]) $geany_spi_nand(clk, cs_n[1], mosi, nand_out);
    always @(clk or cs_n[2]) $geany_spi_eeprom(clk, cs_n[2], mosi, eeprom_out);

    always @(clk or cs_n[3]) $geany_spi_psram(clk, cs_n[3], mosi, psram_out, "psram.hex");

    always @(clk or cs_n[4]) $geany_spi_lua(clk, cs_n[4], mosi, lua_out, "device.lua");

    reg [7:0] received;
    task transfer(input [7:0] data);
        for (integer bit_index = 7; bit_index >= 0; bit_index--) begin
            mosi = data[bit_index];
            #1 clk = 1;
            #1 received[bit_index] = miso;
            clk = 0;
        end
    endtask

    task start(input integer device);
        cs_n[device] = 0;
        #1;
    endtask

    task stop;
        cs_n = '1;
        #1 if (miso !== 1'bz) $fatal(1, "SPI device held MISO after deselection");
    endtask

    initial begin
        start(0);
        transfer('h03); transfer(0); transfer(0); transfer('h10); transfer(0);
        if (received !== 'hba) $fatal(1, "NOR preload was %h", received);
        stop();
        start(2); transfer('h06); stop();
        start(2);
        transfer('h02); transfer('h12); transfer('h34); transfer('h9c);
        stop();
        start(2);
        transfer('h03); transfer('h12); transfer('h34); transfer(0);
        if (received !== 'h9c) $fatal(1, "EEPROM read was %h", received);
        stop();
        start(1); transfer('h06); stop();
        start(1);
        transfer('h02); transfer(0); transfer('h10); transfer('hc3);
        stop();
        start(1);
        transfer('h10); transfer(0); transfer(0); transfer(2);
        stop();
        start(1);
        transfer('h13); transfer(0); transfer(0); transfer(2);
        stop();
        start(1);
        transfer('h03); transfer(0); transfer('h10); transfer(0); transfer(0);
        if (received !== 'hc3) $fatal(1, "NAND cache read was %h", received);
        stop();
        start(3);
        transfer('h0b); transfer('h7f); transfer('hff); transfer('hfe); transfer(0); transfer(0);
        if (received !== 'hba) $fatal(1, "PSRAM fast read was %h", received);
        transfer(0);
        if (received !== 'hbe) $fatal(1, "PSRAM second preload byte was %h", received);
        stop();
        start(3);
        transfer('h02); transfer('h7f); transfer('hff); transfer('hfe); transfer('h00); transfer('hff);
        stop();
        start(3);
        transfer('h03); transfer('h7f); transfer('hff); transfer('hfe); transfer(0);
        if (received !== 'h00) $fatal(1, "PSRAM write was %h", received);
        transfer(0);
        if (received !== 'hff) $fatal(1, "PSRAM overwrite was %h", received);
        stop();
        start(4);
        transfer('h9f);
        stop();
        start(4);
        transfer(0);
        if (received !== 'h12) $fatal(1, "Lua SPI first byte was %h", received);
        transfer(0);
        if (received !== 'h34) $fatal(1, "Lua SPI second byte was %h", received);
        stop();
        $display("SPI memory test passed");
        $finish;
    end
endmodule
