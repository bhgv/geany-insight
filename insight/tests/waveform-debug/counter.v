// EN: A small register and wire for the waveform runner smoke test.
// RU: Простой регистр и провод для проверки запуска симуляции.

module counter (
    input wire clk,
    output reg [3:0] count = 0,
    output wire odd
);
    // EN: Expose the low bit as a separate signal.
    // RU: Выводим младший бит как отдельный сигнал.
    assign odd = count[0];

    // EN: Increment the register on each rising edge.
    // RU: Увеличиваем регистр по каждому фронту.
    always @(posedge clk)
        count <= count + 1'b1;
endmodule
