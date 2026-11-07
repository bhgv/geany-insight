`timescale 1ns/1ns
module sdram_tb;
    reg ck = 0, cke = 1, cs_n = 0;
    reg [2:0] command = 7;
    reg [12:0] a = 0;
    reg [1:0] ba = 0, dqm = 0;
    reg host = 0;
    reg [15:0] host_word = 0, device_word = 'z;
    wire [15:0] dq = host ? host_word : 16'bz;
    assign dq = device_word;
    always @(ck)
        $geany_sdram(ck, cke, cs_n, command[2], command[1], command[0],
                     a, ba, dq, dqm, device_word, "sdram.hex");

    task automatic step(input [2:0] op, input [1:0] bank, input [12:0] address);
        command = op;
        ba = bank;
        a = address;
        #2 ck = 1;
        #2 ck = 0;
        #1;
    endtask

    initial begin
        step(0, 0, 'h20); // BL1, CAS 2
        step(3, 2, 1);
        step(5, 2, 4);
        step(7, 0, 0);
        if (dq !== 16'bz) $fatal(1, "SDRAM read arrived early");
        step(7, 0, 0);
        if (dq !== 'hbeef) $fatal(1, "SDRAM preload read: %h", dq);
        step(7, 0, 0);
        if (dq !== 16'bz) $fatal(1, "SDRAM held bus after read");
        dqm = 2;
        host = 1;
        host_word = 'h1234;
        step(4, 2, 4);
        host = 0;
        dqm = 0;
        step(7, 0, 0);
        step(5, 2, 'h404); // A10 requests auto-precharge
        step(7, 0, 0);
        step(7, 0, 0);
        if (dq !== 'hbe34) $fatal(1, "SDRAM masked write readback: %h", dq);
        step(5, 2, 4);
        step(7, 0, 0);
        step(7, 0, 0);
        if (dq !== 16'bz) $fatal(1, "SDRAM auto-precharge did not close the bank");
        $display("SDRAM test passed");
        $finish;
    end
endmodule
