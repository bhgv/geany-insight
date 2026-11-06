`timescale 1ns/1ns
module live_devices(
    input wire enable, rx,
    output wire ready, tx,
    output reg [3:0] addr,
    inout wire [7:0] dq,
    output reg ce_n, we_n, oe_n,
    output reg [7:0] result
);
    reg drive;
    assign ready = enable;
    assign tx = rx;
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
endmodule
