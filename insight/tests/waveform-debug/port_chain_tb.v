// EN: Record a two-hop port chain and a sibling branch for HDL Uses.
// RU: Записывает двухзвенную цепочку портов и соседнюю ветку для HDL Uses.
`timescale 1ns/1ns

module port_chain_leaf(input wire sample);
endmodule

module port_chain_branch(input wire incoming);
    port_chain_leaf sink(.sample(incoming));
endmodule

module port_chain_tb;
    reg stimulus = 0;
    port_chain_branch left_branch(.incoming(stimulus));
    port_chain_branch right_branch(.incoming(stimulus));

    initial begin
        $dumpfile("port_chain.vcd");
        $dumpvars(0, port_chain_tb);
        #5 stimulus = 1;
        #5 $finish;
    end
endmodule
