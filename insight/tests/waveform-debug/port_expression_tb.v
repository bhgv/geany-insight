// EN: Record an input expression and a split output for HDL Uses.
// RU: Записывает выражение на входе и разделённый выход для HDL Uses.
`timescale 1ns/1ns

module port_expression_leaf(input wire value, output wire [1:0] result);
    assign result = {value, ~value};
endmodule

module port_expression_tb;
    reg left_input = 0;
    reg right_input = 0;
    wire upper_output;
    wire lower_output;
    port_expression_leaf dut(
        .value(left_input ^ right_input),
        .result({upper_output, lower_output})
    );

    initial begin
        $dumpfile("port_expression.vcd");
        $dumpvars(0, port_expression_tb);
        #5 left_input = 1;
        #5 right_input = 1;
        #5 $finish;
    end
endmodule
