// Copyright (c) 2026 bhgv
// EN: Read EDID over DDC and verify HPD, repeated START, and write rejection.
// RU: Читает EDID по DDC и проверяет HPD, повторный START и отказ записи.
`timescale 1ns/1ns
module ddc_peer #(parameter bit CUSTOM = 0) (output reg done = 0);
    reg plug = 0, hpd = 0, clock_low = 0, data_low = 0;
    reg drive = 1'bz;
    tri1 scl, sda;
    assign scl = clock_low ? 1'b0 : 1'bz;
    assign sda = data_low ? 1'b0 : 1'bz;
    assign sda = drive;

    generate
        if (CUSTOM) begin
            always @(scl or sda or plug)
                $geany_hdmi_ddc(scl, sda, plug, drive, hpd, "edid.hex");
        end else begin
            always @(scl or sda or plug)
                $geany_hdmi_ddc(scl, sda, plug, drive, hpd);
        end
    endgenerate

    // EN: Start or restart an I²C transfer.
    // RU: Начинает или повторно запускает обмен I²C.
    task start;
        data_low = 0;
        #1 clock_low = 0;
        #1 data_low = 1;
        #1 clock_low = 1;
    endtask

    // EN: Finish the transfer with a STOP condition.
    // RU: Завершает обмен условием STOP.
    task stop;
        data_low = 1;
        #1 clock_low = 0;
        #1 data_low = 0;
        #1;
    endtask

    // EN: Write value and compare the target acknowledgment with ack.
    // RU: Пишет value и сверяет подтверждение устройства с ack.
    task send_byte(input [7:0] value, input bit ack);
        for (integer i = 7; i >= 0; --i) begin
            data_low = !value[i];
            #1 clock_low = 0;
            #1 clock_low = 1;
            #1;
        end
        data_low = 0;
        #1 clock_low = 0;
        #1 if (sda !== !ack)
            $fatal(1, "Unexpected EDID ACK for %h", value);
        clock_low = 1;
        #1;
    endtask

    // EN: Read value and drive the requested master acknowledgment ack.
    // RU: Читает value и выдаёт указанное подтверждение ведущего ack.
    task read_byte(output reg [7:0] value, input bit ack);
        for (integer i = 7; i >= 0; --i) begin
            #1 clock_low = 0;
            #1 value[i] = sda;
            clock_low = 1;
            #1;
        end
        data_low = ack;
        #1 clock_low = 0;
        #1 clock_low = 1;
        #1 data_low = 0;
    endtask

    reg [7:0] value, sum;
    initial begin
        #1 if (hpd !== 0) $fatal(1, "HPD asserted before connection");
        start();
        send_byte(8'ha0, 0);
        stop();
        plug = 1;
        #1 if (hpd !== 1) $fatal(1, "HPD did not assert");

        start();
        send_byte(8'ha0, 1);
        send_byte(8'h00, 1);
        send_byte(8'h99, 0);
        stop();
        start();
        send_byte(8'ha0, 1);
        send_byte(CUSTOM ? 8'h80 : 8'h00, 1);
        start();
        send_byte(8'ha1, 1);
        sum = 0;
        for (integer i = 0; i < 128; ++i) begin
            read_byte(value, i != 127);
            if (i == 0 && value !== (CUSTOM ? 8'h02 : 8'h00))
                $fatal(1, "Wrong EDID block or write changed read-only data");
            if (!CUSTOM && i == 54 && value !== 8'hd6)
                $fatal(1, "Missing default detailed timing");
            sum += value;
        end
        stop();
        if (sum !== 0) $fatal(1, "Bad EDID checksum: %h", sum);
        plug = 0;
        #1 if (hpd !== 0 || sda !== 1) $fatal(1, "Unplug did not release DDC");
        done = 1;
    end
endmodule

module hdmi_ddc_tb;
    wire default_done, custom_done;
    ddc_peer builtin(default_done);
    ddc_peer #(1) custom_image(custom_done);

    initial begin
        wait (default_done && custom_done);
        $display("HDMI DDC passed");
        $finish;
    end

    initial #10000 $fatal(1, "HDMI DDC test timed out");
endmodule
