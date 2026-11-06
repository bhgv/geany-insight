`timescale 1ns/1ns
module memory_image_tb;
    reg ck = 0, reset_n = 0, cs_n = 1;
    reg [2:0] command = 7;
    reg [15:0] address = 0;
    reg [7:0] model_data = 'z;
    reg model_dqs = 1'bz;
    wire [7:0] dq = model_data;
    wire dqs = model_dqs;
    reg [3:0] sram_address = 3;
    reg [15:0] sram_drive = 'z;
    wire [15:0] sram_dq = sram_drive;
    reg ce_n = 1, we_n = 1, oe_n = 0;
    wire cke = 1'b1, dm = 1'b0;
    wire [2:0] bank = 5;

    always @(ck or reset_n or dqs)
        $geany_ddr3(ck, reset_n, cke, cs_n, command[2], command[1], command[0],
                   address, bank, dq, dqs, dm, 1, 1, model_data, model_dqs, "ddr.hex");

    always @(sram_address or sram_dq or ce_n or we_n or oe_n)
        $geany_sram(sram_address, sram_dq, ce_n, we_n, oe_n, sram_drive, "sram.hex");

    task issue(input [2:0] op, input [15:0] addr);
        command = op;
        address = addr;
        cs_n = 0;
        #1 ck = 1;
        #1 ck = 0;
        cs_n = 1;
    endtask

    initial begin
        #1 ce_n = 0;
        #1 if (sram_dq !== 16'hbeef)
            $fatal(1, "SRAM preload was %h", sram_dq);
        sram_address = 4;
        #1 if (sram_dq !== 16'h0123)
            $fatal(1, "SRAM sequential preload was %h", sram_dq);
        reset_n = 1;
        issue(3'b011, 16'h8123);
        issue(3'b101, 16'h0400);
        for (integer beat = 0; beat < 8; beat++) begin
            ck = !ck;
            #1;
            if (dq !== (8'h80 + 8'(beat)) || dqs !== ck)
                $fatal(1, "DDR3 preload beat %0d was %h / %b", beat, dq, dqs);
        end
        ck = !ck;
        #1 if (dq !== 8'bz || dqs !== 1'bz)
            $fatal(1, "DDR3 did not release the data bus");
        $display("Memory image test passed");
        $finish;
    end
endmodule
