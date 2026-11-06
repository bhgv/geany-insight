`timescale 1ns/1ns
module external_memory #(parameter [7:0] DATA = 0)(
    input wire enable,
    input wire [7:0] mask,
    output wire [7:0] data
);
    assign data = enable ? (DATA ^ mask) : 8'h00;
endmodule
