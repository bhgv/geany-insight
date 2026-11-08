// Copyright (c) 2026 bhgv
// EN: Verify asynchronous PSRAM preload, byte lanes and four-state bus behavior.
// RU: Проверяет загрузку PSRAM, выбор байтов и четырёхзначное состояние шины.
`timescale 1ns/1ns
module parallel_psram_tb;
    reg [19:0] a = 0;
    reg ce_n = 1, we_n = 1, oe_n = 1, lb_n = 1, ub_n = 1;
    reg [15:0] host = 'z, memory = 'z;
    wire [15:0] dq = host;
    assign dq = memory;

    always @(a or dq or ce_n or we_n or oe_n or lb_n or ub_n)
        $geany_parallel_psram(a, dq, ce_n, we_n, oe_n, lb_n, ub_n, memory, "psram-parallel.hex");

    // EN: Read with active-low lane enables lower_n and upper_n and compare expected.
    // RU: Читает при активных низким уровнем lower_n и upper_n и сверяет expected.
    task check_read(input bit lower_n, input bit upper_n, input [15:0] expected);
        lb_n = lower_n;
        ub_n = upper_n;
        ce_n = 0;
        oe_n = 0;
        #2;
        if (dq !== expected)
            $fatal(1, "PSRAM[%h] was %h, expected %h", a, dq, expected);
        oe_n = 1;
        #2;
        if (dq !== 16'hzzzz)
            $fatal(1, "PSRAM did not release DQ after OE#");
    endtask

    initial begin
        #2 a = 20'hfffff;
        check_read(0, 0, 16'hbeef);
        check_read(1, 0, 16'hbezz);
        check_read(0, 1, 16'hzzef);

        host = 16'h12a5;
        we_n = 0;
        #2 we_n = 1;
        host = 'z;
        #2;
        check_read(0, 0, 16'hbea5);

        a = 20'h90000;
        check_read(0, 0, 16'hxxxx);
        host = 16'b10101010_000000xz;
        we_n = 0;
        #2 lb_n = 1;
        #2 ce_n = 1;
        we_n = 1;
        host = 'z;
        #2;
        check_read(0, 0, 16'b10101010_000000xz);
        ce_n = 1;
        oe_n = 0;
        #2;
        if (dq !== 16'hzzzz)
            $fatal(1, "PSRAM drove DQ while deselected");
        $display("Parallel PSRAM device passed");
        $finish;
    end
endmodule
