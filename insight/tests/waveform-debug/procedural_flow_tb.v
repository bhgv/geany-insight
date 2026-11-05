// EN: Record sampled data through a clocked register and an output assignment.
// RU: Записывает прохождение данных через тактируемый регистр и выходное присваивание.
`timescale 1ns/1ns

module procedural_flow_leaf(
    input wire clk,
    input wire enable,
    input wire data_in,
    output wire data_out
);
    reg sampled = 0;

    always @(posedge clk) begin
        if (enable)
            sampled <= data_in;
    end

    assign data_out = sampled;
endmodule

module procedural_flow_tb;
    reg clk = 0;
    reg enable = 1;
    reg stimulus = 0;
    wire result;

    procedural_flow_leaf dut(
        .clk(clk), .enable(enable), .data_in(stimulus), .data_out(result)
    );

    initial begin
        $dumpfile("procedural_flow.vcd");
        $dumpvars(0, procedural_flow_tb);
        #3 stimulus = 1;
        #2 clk = 1;
        #5 $finish;
    end
endmodule
