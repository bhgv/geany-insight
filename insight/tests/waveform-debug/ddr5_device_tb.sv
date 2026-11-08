// Copyright (c) 2026 bhgv
// EN: Verify DDR5 CA decoding, BL16 preload, masked write and auto-precharge.
// RU: Проверяет декодирование CA DDR5, образ BL16, маску записи и автопредзаряд.
`timescale 1ns/1ns
module ddr5_device_tb;
    localparam [4:0] BANK = 29;
    localparam [15:0] ROW = 16'h1234;
    localparam [13:0] BANK_CA = {3'b000, BANK, 6'b000000};
    reg ck = 0, reset_n = 0, cs_n = 1, dm_n = 1;
    reg [13:0] ca = 0;
    reg [7:0] model_dq = 'z, host_dq = 0;
    reg model_dqs = 1'bz, host_dqs = 0, host_drive = 0;
    wire [7:0] dq = host_drive ? host_dq : 8'bz;
    wire dqs = host_drive ? host_dqs : 1'bz;
    assign dq = model_dq;
    assign dqs = model_dqs;

    always @(ck or reset_n or dqs)
        $geany_ddr5(ck, reset_n, cs_n, ca, dq, dqs, dm_n,
                    2, 1, model_dq, model_dqs, "ddr5.hex");

    // EN: Sample CA word on a rising CK with chip select select_n.
    // RU: Выдаёт слово CA word с выбором select_n на нарастающем фронте CK.
    task command_cycle(input bit select_n, input [13:0] word);
        cs_n = select_n;
        ca = word;
        #2 ck = 1;
        #2 ck = 0;
        #2;
    endtask

    // EN: Activate ROW in BANK. cancel holds CS# low on the second cycle.
    // RU: Активирует ROW в BANK. При cancel сигнал CS# остаётся низким на втором такте.
    task activate(input bit cancel);
        command_cycle(0, BANK_CA | {8'b0, ROW[3:0], 2'b00});
        command_cycle(!cancel, {2'b00, ROW[15:4]});
    endtask

    // EN: Issue command op at column 0x20. close_bank requests auto-precharge.
    // RU: Выдаёт команду op для столбца 0x20. close_bank задаёт автопредзаряд.
    task access_command(input [4:0] op, input bit close_bank);
        command_cycle(0, BANK_CA | {9'b0, op});
        command_cycle(1, {3'b000, !close_bank, 2'b00, 8'h08});
    endtask

    // EN: Verify an entire BL16 read. updated selects the post-write values.
    // RU: Проверяет чтение BL16. updated выбирает ожидаемые значения после записи.
    task read_burst(input bit updated);
        reg [7:0] expected;
        command_cycle(1, 0);
        for (integer beat = 0; beat < 16; ++beat) begin
            ck = !ck;
            #2;
            expected = updated && beat != 5 ? 8'hb0 + 8'(beat) : 8'ha0 + 8'(beat);
            if (dq !== expected || dqs !== ck)
                $fatal(1, "DDR5 beat %0d was %h/%b, expected %h", beat, dq, dqs, expected);
        end
        ck = 1;
        #2;
        if (dq !== 8'bz || dqs !== 1'bz)
            $fatal(1, "DDR5 did not release DQ and DQS after BL16");
        ck = 0;
        #2;
    endtask

    // EN: Check that a read from a closed bank never starts a burst.
    // RU: Проверяет, что чтение из закрытого банка не запускает пакет.
    task check_closed;
        access_command(5'h1d, 0);
        repeat (3) begin
            #2 ck = 1;
            #2 if (dq !== 8'bz || dqs !== 1'bz)
                $fatal(1, "DDR5 accessed a closed bank");
            ck = 0;
        end
    endtask

    initial begin
        #2 reset_n = 1;
        activate(1);
        check_closed();
        activate(0);
        access_command(5'h1d, 0);
        read_burst(0);

        access_command(5'h0d, 0);
        command_cycle(1, 0);
        for (integer beat = 0; beat < 16; ++beat) begin
            host_dq = 8'hb0 + 8'(beat);
            host_dqs = beat[0];
            dm_n = beat != 5;
            host_drive = 1;
            #2;
        end
        host_drive = 0;
        #2;
        access_command(5'h1d, 1);
        read_burst(1);
        check_closed();

        activate(0);
        command_cycle(0, BANK_CA | 14'h001b);
        check_closed();
        $display("DDR5 device passed");
        $finish;
    end
endmodule
