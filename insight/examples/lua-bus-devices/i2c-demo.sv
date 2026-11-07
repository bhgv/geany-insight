`timescale 1ns/1ns

// Open this file and connect i2c-frame.lua in Port Signals.
module i2c_demo(inout wire scl, sda);
    reg scl_low = 0, sda_low = 0;
    assign scl = scl_low ? 1'b0 : 1'bz;
    assign sda = sda_low ? 1'b0 : 1'bz;

    task automatic start;
        sda_low = 0;
        #5 scl_low = 0;
        #5 sda_low = 1;
        #5 scl_low = 1;
    endtask

    task automatic stop;
        sda_low = 1;
        #5 scl_low = 0;
        #5 sda_low = 0;
        #5;
    endtask

    task automatic write_byte(input [7:0] value);
        for (int i = 7; i >= 0; i--) begin
            sda_low = !value[i];
            #5 scl_low = 0;
            #5 scl_low = 1;
        end
        sda_low = 0;
        #5 scl_low = 0;
        #5 if (sda !== 1'b0) $fatal(1, "I2C NACK after %h", value);
        scl_low = 1;
        #5;
    endtask

    task automatic read_byte(output reg [7:0] value, input bit ack);
        sda_low = 0;
        for (int i = 7; i >= 0; i--) begin
            #5 scl_low = 0;
            #5 value[i] = sda;
            scl_low = 1;
        end
        sda_low = ack;
        #5 scl_low = 0;
        #5 scl_low = 1;
        #5 sda_low = 0;
    endtask

    reg [7:0] first, second;
    initial begin
        #10 start();
        write_byte(8'ha0);
        write_byte(8'h10);
        write_byte(8'h25);
        start();
        write_byte(8'ha1);
        read_byte(first, 1);
        read_byte(second, 0);
        stop();
        if ({first, second} !== 16'h1035)
            $fatal(1, "Unexpected I2C reply: %h %h", first, second);
        $display("I2C Lua reply: %h %h", first, second);
    end
endmodule
