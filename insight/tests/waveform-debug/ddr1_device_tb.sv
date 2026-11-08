// Copyright (c) 2026 bhgv
// EN: Verify DDR1 preload, BL4 wrapping, DQS writes, masking and auto-precharge.
// RU: Проверяет загрузку DDR1, переходы BL4, запись по DQS, маску и автопредзаряд.
`timescale 1ns/1ns
module ddr1_device_tb;
    reg ck = 0, cke = 1, cs_n = 1, ras_n = 1, cas_n = 1, we_n = 1;
    reg [12:0] a = 0;
    reg [1:0] ba = 2;
    reg [7:0] model_dq = 'z, host_dq = 0;
    reg model_dqs = 1'bz, host_dqs = 0, host_drive = 0, dm = 0;
    wire [7:0] dq = host_drive ? host_dq : 8'bz;
    wire dqs = host_drive ? host_dqs : 1'bz;
    assign dq = model_dq;
    assign dqs = model_dqs;

    always @(ck or dqs)
        $geany_ddr1(ck, cke, cs_n, ras_n, cas_n, we_n, a, ba,
                    dq, dqs, dm, 2, 1, model_dq, model_dqs, "ddr1.hex");

    // EN: Issue command code cmd with address at the next CK rising edge.
    // RU: Выдаёт команду cmd с адресом address на следующем фронте CK.
    task command(input [2:0] cmd, input [12:0] address);
        {ras_n, cas_n, we_n} = cmd;
        a = address;
        cs_n = 0;
        #2 ck = 1;
        #2 ck = 0;
        cs_n = 1;
        #2;
    endtask

    // EN: Read four beats and compare against packed expected, first byte at the MSB.
    // RU: Читает четыре передачи и сверяет expected, первый байт в старших битах.
    task read_burst(input [31:0] expected);
        #2 ck = 1;
        #2 ck = 0;
        #2;
        for (integer beat = 0; beat < 4; beat++) begin
            ck = !ck;
            #2;
            if (dq !== expected[31-beat*8 -: 8] || dqs !== ck)
                $fatal(1, "DDR1 beat %0d returned %h/%b", beat, dq, dqs);
        end
        ck = 1;
        #2;
        if (dq !== 8'bz || dqs !== 1'bz)
            $fatal(1, "DDR1 did not release DQ/DQS after BL4");
        ck = 0;
        #2;
    endtask

    initial begin
        command(3, 13'h123);
        command(5, 13'h002);
        read_burst(32'ha2a3a0a1);
        command(4, 13'h002);
        #2 ck = 1;
        #2 ck = 0;
        for (integer beat = 0; beat < 4; beat++) begin
            dm = beat == 1;
            host_dq = 8'hb0 + 8'(beat);
            host_dqs = beat[0];
            host_drive = 1;
            #2;
        end
        host_drive = 0;
        #2;
        command(5, 13'h402);
        read_burst(32'hb0a3b2b3);
        command(5, 13'h002);
        repeat (3) begin
            #2 ck = 1;
            #2 if (dq !== 8'bz || dqs !== 1'bz)
                $fatal(1, "DDR1 auto-precharge did not close the bank");
            ck = 0;
        end
        $display("DDR1 device passed");
        $finish;
    end
endmodule
