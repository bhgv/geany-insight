// EN: Drive three rising edges and write one VCD in the simulation directory.
// RU: Формирует три фронта и записывает один VCD в папку симуляции.
`timescale 1ns/1ns

module counter_tb;
    // EN: Keep the clock, bus and low bit visible in the testbench scope.
    // RU: Оставляем тактовый сигнал, шину и младший бит видимыми в тестбенче.
    reg clk = 0;
    wire [3:0] count;
    wire odd;
    counter dut (.clk(clk), .count(count), .odd(odd));

    initial begin
        // EN: A relative filename writes the waveform into the run directory.
        // RU: Относительное имя записывает диаграмму в папку запуска.
        $dumpfile("counter.vcd");
        $dumpvars(0, counter_tb);

        // EN: Rising edges at 5, 15 and 25 ns update count to 1, 2 and 3.
        // RU: Фронты на 5, 15 и 25 нс меняют count на 1, 2 и 3.
        repeat (3) begin
            #5 clk = 1;
            #5 clk = 0;
        end

        // EN: End the run so Geany can open the resulting capture.
        // RU: Завершаем запуск, чтобы Geany открыл запись.
        $finish;
    end
endmodule
