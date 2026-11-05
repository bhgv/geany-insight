// EN: Demonstrate an explicit $stop during an interactive Geany run.
// RU: Показывает явную остановку $stop при интерактивном запуске из Geany.
`timescale 1ns/1ns

module counter_debug_tb;
    // EN: Expose a clock, a four-bit bus and its low bit.
    // RU: Выводим тактовый сигнал, четырёхбитную шину и её младший бит.
    reg clk = 0;
    wire [3:0] count;
    wire odd;
    counter dut (.clk(clk), .count(count), .odd(odd));

    initial begin
        // EN: Start recording before the first clock edge.
        // RU: Начинаем запись до первого фронта.
        $dumpfile("counter_debug.vcd");
        $dumpvars(0, counter_debug_tb);
        #5 clk = 1;
        // EN: Pause after the nonblocking register update has settled.
        // RU: Останавливаемся после выполнения неблокирующего присваивания.
        #1 $stop;
        // EN: Generate a second edge after Continue and finish the capture.
        // RU: После Continue формируем второй фронт и завершаем запись.
        #4 clk = 0;
        #5 clk = 1;
        #5 $finish;
    end
endmodule
