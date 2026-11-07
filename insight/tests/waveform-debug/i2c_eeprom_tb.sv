`timescale 1ns/1ns
module i2c_eeprom_tb;
    reg scl_low = 0, sda_low = 0;
    reg device_drive = 1'bz, lua_drive = 1'bz;
    tri1 scl, sda;
    assign scl = scl_low ? 1'b0 : 1'bz;
    assign sda = sda_low ? 1'b0 : 1'bz;
    assign sda = device_drive;
    assign sda = lua_drive;
    always @(scl or sda) $geany_i2c_eeprom(scl, sda, 'h50, device_drive, "i2c.hex");

    always @(scl or sda) $geany_i2c_lua(scl, sda, 'h51, lua_drive, "device.lua");

    task start;
        sda_low = 0;
        #1 scl_low = 0;
        #1 sda_low = 1;
        #1 scl_low = 1;
    endtask

    task stop;
        sda_low = 1;
        #1 scl_low = 0;
        #1 sda_low = 0;
        #1;
    endtask

    task send_byte(input [7:0] value, input bit ack);
        for (integer i = 7; i >= 0; i--) begin
            sda_low = !value[i];
            #1 scl_low = 0;
            #1 scl_low = 1;
            #1;
        end
        sda_low = 0;
        #1 scl_low = 0;
        #1 if (sda !== !ack) $fatal(1, "Unexpected ACK for %h", value);
        scl_low = 1;
        #1;
    endtask

    task read_byte(input [7:0] expected, input bit ack);
        reg [7:0] actual;
        for (integer i = 7; i >= 0; i--) begin
            #1 scl_low = 0;
            #1 actual[i] = sda;
            scl_low = 1;
            #1;
        end
        if (actual !== expected) $fatal(1, "Read %h, expected %h", actual, expected);
        sda_low = ack;
        #1 scl_low = 0;
        #1 scl_low = 1;
        #1 sda_low = 0;
    endtask

    initial begin
        #1 start();
        send_byte('ha4, 0);
        stop();
        start();
        send_byte('ha0, 1); send_byte('h12, 1); send_byte('h34, 1);
        start();
        send_byte('ha1, 1);
        read_byte('hbe, 1); read_byte('hef, 0);
        stop();
        start();
        send_byte('ha0, 1); send_byte('h12, 1); send_byte('h7f, 1);
        send_byte('h3c, 1); send_byte('h5a, 1);
        stop();
        start();
        send_byte('ha0, 1); send_byte('h12, 1); send_byte('h00, 1);
        start();
        send_byte('ha1, 1);
        read_byte('h5a, 0);
        stop();
        start();
        send_byte('ha2, 1); send_byte('h6a, 1); send_byte('h37, 1);
        start();
        send_byte('ha3, 1);
        read_byte('h6a, 1); read_byte('h02, 1); read_byte('h56, 0);
        stop();
        $display("I2C EEPROM test passed");
        $finish;
    end
endmodule
