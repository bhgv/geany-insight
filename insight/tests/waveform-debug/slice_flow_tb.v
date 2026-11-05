// EN: Keep a packed range and a single bit visible in a waveform.
// RU: Оставляет упакованный диапазон и один бит видимыми на диаграмме.
`timescale 1ns/1ns

module slice_flow_leaf(input wire [3:0] part, output wire bit_out);
    assign bit_out = part[2];
endmodule

module slice_flow_tb;
    reg [7:0] bus = 0;
    wire result;
    slice_flow_leaf dut(.part(bus[7:4]), .bit_out(result));

    initial begin
        $dumpfile("slice_flow.vcd");
        $dumpvars(0, slice_flow_tb);
        #5 bus = 8'h40;
        #5 $finish;
    end
endmodule
