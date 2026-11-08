// Copyright (c) 2026 bhgv
// EN: Verify synchronous USART loopback with both sample edges and a stopped clock.
// RU: Проверяет обмен USART на обоих фронтах и при остановленном такте.
`timescale 1ns/1ns
module usart_peer #(parameter RISING = 1, parameter [7:0] PAYLOAD = 8'ha6)
                   (output reg done = 0);
    reg xck = 0, enabled = 0, rx = 1, request = 0, ack = 0;
    reg [7:0] payload = PAYLOAD;
    wire tx = rx;

    always #5 if (enabled) xck = ~xck;
    always @(xck) $geany_usart(xck, tx, rx, payload, request, ack, RISING[0]);

    initial begin
        #3 request = 1;
        #20 if (ack !== 0 || rx !== 1)
            $fatal(1, "USART advanced while XCK was stopped");
        enabled = 1;
        wait (ack === 1);
        if (rx !== 1)
            $fatal(1, "USART acknowledged before the stop bit");
        #1 payload = PAYLOAD ^ 8'hff;
        request = 0;
        wait (ack === 0);
        done = 1;
    end
endmodule

module usart_tb;
    wire rising_done, falling_done;
    usart_peer #(1, 8'ha6) rising(rising_done);
    usart_peer #(0, 8'h39) falling(falling_done);

    initial begin
        wait (rising_done && falling_done);
        $display("USART device passed");
        $finish;
    end

    initial begin
        #5000;
        $fatal(1, "USART test timed out");
    end
endmodule
