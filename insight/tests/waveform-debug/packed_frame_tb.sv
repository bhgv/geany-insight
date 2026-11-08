// Copyright (c) 2026 bhgv
// EN: Keep distinct packed structs live in two automatic task activations.
// RU: Сохраняет разные packed struct в двух активациях автоматической задачи.
`timescale 1ns/1ns
module packed_frame_tb;
    typedef struct packed {
        logic valid;
        logic [2:0] code;
    } tag_t;

    typedef struct packed {
        tag_t tag;
        logic [3:0] payload;
    } packet_t;

    typedef union packed {
        packet_t packet;
        logic [7:0] raw;
    } overlay_t;

    packet_t storage [0:1];
    overlay_t overlay;

    // EN: Save seed in an independent packed local and suspend the activation.
    // RU: Сохраняет seed в независимой packed-переменной и приостанавливает активацию.
    task automatic worker(input logic [7:0] seed);
        packet_t packet;
        packet = seed;
        #10 $display("packet = %b", packet);
    endtask

    initial worker(8'b1010_0110);
    initial worker(8'b0101_1001);
    initial begin
        overlay = 8'b0011_1010;
        storage[0] = 8'b1100_0011;
        storage[1] = 8'b0011_1100;
        #1 $display("Both packed frames are waiting.");
        #20 $finish;
    end
endmodule
