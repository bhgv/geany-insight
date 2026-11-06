`timescale 1ns/1ns
module ddr4_device_tb;
    reg ck = 0, reset_n = 0, cke = 1, cs_n = 1, act_n = 1;
    reg [16:0] address = 0;
    reg [1:0] ba = 1, bg = 2;
    reg [7:0] host_data = 0, model_data = 'z;
    reg host_dqs = 0, model_dqs = 1'bz, host_drive = 0;
    reg dm_n = 1;
    wire [7:0] dq = host_drive ? host_data : 8'bz;
    wire dqs = host_drive ? host_dqs : 1'bz;
    assign dq = model_data;
    assign dqs = model_dqs;

    always @(ck or reset_n or dqs)
        $geany_ddr4(ck, reset_n, cke, cs_n, act_n, address, ba, bg,
                   dq, dqs, dm_n, 1, 1, model_data, model_dqs);

    task command(input act, input [16:0] word);
        begin
            act_n = act;
            address = word;
            cs_n = 0;
            #1 ck = 1;
            #1 ck = 0;
            cs_n = 1;
        end
    endtask

    initial begin
        #1 reset_n = 1;
        command(0, 17'h00123);
        command(1, 17'h10000);
        #1 ck = 1;
        #1 ck = 0;
        for (integer beat = 0; beat < 8; beat++) begin
            host_data = 8'ha0 + beat;
            host_dqs = beat[0];
            host_drive = 1;
            #1;
        end
        host_drive = 0;
        #1;
        command(1, 17'h14400);
        for (integer beat = 0; beat < 8; beat++) begin
            ck = !ck;
            #1;
            if (dq !== (8'ha0 + 8'(beat)) || dqs !== ck)
                $fatal(1, "DDR4 beat %0d was %h / %b", beat, dq, dqs);
        end
        ck = !ck;
        #1;
        if (dq !== 8'bz || dqs !== 1'bz)
            $fatal(1, "DDR4 did not release the data bus");
        ck = 0;
        #1;
        command(1, 17'h14000);
        #1 ck = 1;
        #1 if (dq !== 8'bz)
            $fatal(1, "DDR4 auto-precharge did not close the bank");
        $display("DDR4 device passed");
        $finish;
    end
endmodule
