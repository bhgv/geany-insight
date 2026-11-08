// Copyright (c) 2026 bhgv
// EN: Send the same two-line image through RGB, OpenLDI and TMDS adapters.
// RU: Передаёт одно изображение из двух строк через RGB, OpenLDI и TMDS.
`timescale 1ns/1ns
module raster_displays_tb;
    reg pclk = 0, de = 0, vs = 0;
    reg [5:0] r6 = 0, g6 = 0, b6 = 0;
    reg [7:0] r8 = 0, g8 = 0, b8 = 0;
    reg lclk = 0, hclk = 0;
    reg [2:0] lanes6 = 0;
    reg [3:0] jeida = 0, vesa = 0, link_b = 0;
    reg [6:0] previous_v0 = 0, previous_v1 = 0, previous_v2 = 0, previous_v3 = 0;
    reg [2:0] hdmi = 0, dvi = 0;

    always @(posedge pclk) begin
        $geany_raster(pclk, de, vs, r6, g6, b6, 2, 2, 6, 1, 0, 0, 0);
        $geany_raster(pclk, de, vs, r8, g8, b8, 2, 2, 8, 1, 0, 0, 1);
    end
    always @(posedge lclk) begin
        $geany_raster(lclk, lanes6[0], lanes6[1], lanes6[2], 1'b0, 1'b0, 2, 2, 6, 1, 1, 0, 2);
        $geany_raster(lclk, jeida[0], jeida[1], jeida[2], jeida[3], 1'b0, 2, 2, 8, 1, 1, 0, 3);
        $geany_raster(lclk, vesa[0], vesa[1], vesa[2], vesa[3], 1'b0, 2, 2, 8, 1, 2, 2, 4);
        $geany_raster_dual(lclk, jeida[0], jeida[1], jeida[2], jeida[3], 1'b0,
                           4, 2, 8, 1, 1, 0, 7, link_b[0], link_b[1], link_b[2], link_b[3], 0);
        $geany_raster_dual(lclk, lanes6[0], lanes6[1], lanes6[2], 1'b0, 1'b0,
                           4, 2, 6, 1, 1, 0, 8, link_b[0], link_b[1], link_b[2], 1'b0, 1);
    end
    always @(posedge hclk) begin
        $geany_raster(hclk, hdmi[0], hdmi[1], hdmi[2], 1'b0, 1'b0, 2, 2, 8, 1, 3, 0, 5);
        $geany_raster(hclk, dvi[0], dvi[1], dvi[2], 1'b0, 1'b0, 2, 2, 6, 1, 4, 0, 6);
    end

    // EN: Drive parallel pixel rgb with active data and sync flag v.
    // RU: Выдаёт пиксель rgb с признаком active и синхронизацией v.
    task rgb_pixel(input bit active, input bit v, input [23:0] rgb);
        {r8,g8,b8} = rgb;
        {r6,g6,b6} = {r8[7:2],g8[7:2],b8[7:2]};
        de = active;
        vs = v;
        #2 pclk = 1;
        #2 pclk = 0;
    endtask

    // EN: Serialize rgb with active and vertical v in all three OpenLDI layouts.
    // RU: Сериализует rgb с active и v во всех трёх раскладках OpenLDI.
    // EN: Link B sends the complementary color without control flags.
    // RU: Канал B передаёт инвертированный цвет без управляющих флагов.
    task lvds_pixel(input bit active, input bit v, input [23:0] rgb);
        reg [7:0] r, g, b;
        reg [6:0] j0,j1,j2,j3,v0,v1,v2,v3;
        {r,g,b} = rgb;
        j0 = {g[2],r[7:2]};
        j1 = {b[3:2],g[7:3]};
        j2 = {active,v,1'b0,b[7:4]};
        j3 = {1'b0,b[1:0],g[1:0],r[1:0]};
        v0 = {g[0],r[5:0]};
        v1 = {b[1:0],g[5:1]};
        v2 = {active,v,1'b0,b[5:2]};
        v3 = {1'b0,b[7:6],g[7:6],r[7:6]};
        for (integer bit_index = 0; bit_index < 7; ++bit_index) begin
            jeida = {j3[bit_index],j2[bit_index],j1[bit_index],j0[bit_index]};
            if (bit_index < 2)
                vesa = {previous_v3[bit_index+5],previous_v2[bit_index+5],
                        previous_v1[bit_index+5],previous_v0[bit_index+5]};
            else
                vesa = {v3[bit_index-2],v2[bit_index-2],v1[bit_index-2],v0[bit_index-2]};
            link_b = ~jeida;
            if (bit_index >= 5) link_b[2] = 0;
            lanes6 = jeida[2:0];
            if (bit_index == 0) lclk = 1;
            if (bit_index == 3) lclk = 0;
            #4;
        end
        {previous_v3,previous_v2,previous_v1,previous_v0} = {v3,v2,v1,v0};
    endtask

    // EN: Produce a reversible TMDS data word for byte value and inversion flag.
    // RU: Формирует обратимое слово данных TMDS из value с инверсией invert.
    function automatic [9:0] encode(input [7:0] value, input bit invert);
        reg [7:0] q;
        bit xnor_mode;
        xnor_mode = $countones(value) > 4 || ($countones(value) == 4 && !value[0]);
        q[0] = value[0];
        for (integer i = 1; i < 8; ++i)
            q[i] = q[i-1] ^ value[i] ^ xnor_mode;
        return {invert,!xnor_mode,invert ? ~q : q};
    endfunction

    // EN: Serialize HDMI words h0–h2 and DVI words d0–d2 LSB first.
    // RU: Сериализует слова HDMI h0–h2 и DVI d0–d2 младшим битом вперёд.
    task tmds_word(input [9:0] h0,h1,h2,d0,d1,d2);
        for (integer bit_index = 0; bit_index < 10; ++bit_index) begin
            hdmi = {h2[bit_index],h1[bit_index],h0[bit_index]};
            dvi = {d2[bit_index],d1[bit_index],d0[bit_index]};
            if (bit_index == 0) hclk = 1;
            if (bit_index == 5) hclk = 0;
            #4;
        end
    endtask

    // EN: Send color rgb to both TMDS receivers.
    // RU: Передаёт цвет rgb обоим приёмникам TMDS.
    task tmds_pixel(input [23:0] rgb);
        reg [9:0] r,g,b;
        r = encode(rgb[23:16],1);
        g = encode(rgb[15:8],0);
        b = encode(rgb[7:0],1);
        tmds_word(b,g,r,b,g,r);
    endtask

    // EN: Mark an HDMI video period and leave DVI in blanking until the first pixel.
    // RU: Отмечает видеопериод HDMI и оставляет DVI в гашении до первого пикселя.
    task tmds_line;
        repeat (8) tmds_word('h354,'h0ab,'h354,'h354,'h354,'h354);
        repeat (2) tmds_word('h2cc,'h133,'h2cc,'h354,'h354,'h354);
    endtask

    initial begin
        #1;
        rgb_pixel(0,1,0);
        rgb_pixel(1,0,24'hff0000);
        rgb_pixel(1,0,24'h00ff00);
        rgb_pixel(0,0,0);
        rgb_pixel(1,0,24'h0000ff);
        rgb_pixel(1,0,24'ha65a19);
        rgb_pixel(0,0,0);
    end

    initial begin
        #1;
        repeat (3) lvds_pixel(0,1,0);
        lvds_pixel(1,0,24'hff0000);
        lvds_pixel(1,0,24'h00ff00);
        lvds_pixel(0,0,0);
        lvds_pixel(1,0,24'h0000ff);
        lvds_pixel(1,0,24'ha65a19);
        repeat (2) lvds_pixel(0,0,0);
    end

    initial begin
        #1;
        repeat (3) tmds_word('h154,'h354,'h354,'h154,'h354,'h354);
        // HDMI auxiliary period. A video receiver must not treat these as pixels.
        repeat (8) tmds_word('h354,'h0ab,'h0ab,'h354,'h354,'h354);
        repeat (4) tmds_word('h28e,'h133,'h133,'h354,'h354,'h354);
        tmds_line();
        tmds_pixel(24'hff0000);
        tmds_pixel(24'h00ff00);
        tmds_word('h354,'h354,'h354,'h354,'h354,'h354);
        tmds_line();
        tmds_pixel(24'h0000ff);
        tmds_pixel(24'ha65a19);
        tmds_word('h354,'h354,'h354,'h354,'h354,'h354);
    end

    // EN: Follow a corrupt frame with a valid frame to verify resynchronization.
    // RU: После повреждённого кадра выдаёт корректный для проверки синхронизации.
    initial begin
        #3000;
        repeat (3) tmds_word('h154,'h354,'h354,'h154,'h354,'h354);
        tmds_line();
        tmds_pixel(24'h111111);
        tmds_word('h354,'h000,'h000,'h354,'h000,'h000);
        tmds_pixel(24'h222222);
        tmds_word('h354,'h354,'h354,'h354,'h354,'h354);
        tmds_line();
        tmds_pixel(24'h333333);
        tmds_pixel(24'h444444);
        tmds_word('h354,'h354,'h354,'h354,'h354,'h354);

        repeat (3) tmds_word('h154,'h354,'h354,'h154,'h354,'h354);
        tmds_line();
        tmds_pixel(24'h010203);
        tmds_pixel(24'h040506);
        tmds_word('h354,'h354,'h354,'h354,'h354,'h354);
        tmds_line();
        tmds_pixel(24'h070809);
        tmds_pixel(24'h0a0b0c);
        repeat (2) tmds_word('h354,'h354,'h354,'h354,'h354,'h354);
    end

    initial begin
        #6000;
        $finish;
    end
endmodule
