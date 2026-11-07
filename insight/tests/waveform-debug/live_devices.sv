// EN: Drives live instrument and preprocessed port-discovery checks in the GTK test.
// RU: Схема для проверки живых приборов и списка портов после препроцессора в GTK.
`timescale 1ns/1ns
`define GEANY_LIVE_HAS_SLIDER
`define GEANY_LIVE_SLIDER_PORT level
module live_devices(
    input wire enable, rx, switch_on, push,
    input wire [7:0] external_data,
`ifdef GEANY_LIVE_HAS_SLIDER
    input wire [7:0] `GEANY_LIVE_SLIDER_PORT,
`else
    input wire [7:0] inactive_slider,
`endif
    output wire [7:0] model_result, level_out,
    output wire ready, tx,
    output reg pushed = 0,
    output reg [3:0] addr,
    inout wire [7:0] dq,
    output reg ce_n, we_n, oe_n,
    output reg [7:0] result
);
    reg drive;
    assign ready = enable & switch_on;
    always @(posedge push) pushed <= 1;
    assign tx = rx;
    assign model_result = external_data;
    assign level_out = level;
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

`undef GEANY_LIVE_HAS_SLIDER
`undef GEANY_LIVE_SLIDER_PORT
