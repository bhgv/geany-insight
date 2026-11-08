// EN: Keep two automatic task activations alive with distinct local values.
// RU: Удерживает две активации автоматической задачи с разными локальными значениями.
`timescale 1ns/1ns

module process_values_tb;
    reg [3:0] first_result = 0;
    reg [3:0] second_result = 0;
    reg [3:0] samples [-2:1];

    // EN: Keep a private copy across a delay and publish it on return.
    // RU: Сохраняет локальную копию на время задержки и возвращает её.
    task automatic worker(input [3:0] seed, output [3:0] result);
        reg [3:0] saved;
        reg [3:0] local_items[];
        begin
            saved = seed;
            local_items = new[2];
            local_items[0] = seed;
            #10 result = saved;
        end
    endtask

    initial worker(4'd3, first_result);
    initial worker(4'd9, second_result);

    initial begin
        samples[-2] = 4'b10xz;
        samples[1] = 4'b0110;
        $dumpfile("process_values.vcd");
        $dumpvars(0, process_values_tb);
        #1 $display("Both task activations are waiting.");
        #20 $finish;
    end
endmodule
