`timescale 1ns/1ns

// Open this file and connect spi-byte-queue.lua in Port Signals.
module spi_demo(
    output reg sck = 0, cs_n = 1, mosi = 0,
    input wire miso
);
    task automatic transfer(input [7:0] sent, output reg [7:0] received);
        for (int i = 7; i >= 0; i--) begin
            mosi = sent[i];
            #5 sck = 1;
            #5 received[i] = miso;
            sck = 0;
        end
    endtask

    reg [7:0] unused, vendor, family, counter;
    initial begin
        #10 cs_n = 0;
        #5;
        transfer(8'h9f, unused);
        transfer(0, vendor);
        transfer(0, family);
        transfer(0, counter);
        #5 cs_n = 1;
        if ({vendor, family, counter} !== 24'h475901)
            $fatal(1, "Unexpected SPI reply: %h %h %h", vendor, family, counter);
        $display("SPI Lua reply: %h %h %h", vendor, family, counter);
    end
endmodule
