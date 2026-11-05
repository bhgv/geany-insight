module synchronous_steps_tb;
    logic [3:0] samples[];
    int pending[$];
    string label_text;
    real ratio;
    int result;

    function automatic int calculate(input int argument);
        int local_value;
        local_value = argument + 1;
        calculate = local_value * 2;
    endfunction

    initial begin
        samples = new[1];
        samples[0] = 4'b10xz;
        pending.push_back(17);
        label_text = "ready";
        ratio = 1.25;
        result = calculate(6);
        #1 $finish;
    end

    final begin
        result = result + 1;
        $display("Final result: %0d", result);
    end
endmodule
