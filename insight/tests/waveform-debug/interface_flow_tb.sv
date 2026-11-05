interface channel(input logic clk);
    logic [7:0] data;
    logic valid;
    logic [3:0] tag;
    modport producer(output data, valid, .tag_out(tag[3:2]));
    modport consumer(input clk, valid, .high(data[7:4]));
endinterface

module interface_source(channel.producer link, input logic [7:0] payload);
    assign link.data = payload;
    assign link.tag_out = payload[7:6];
    assign link.valid = payload[0];
endmodule

module interface_sink(channel.consumer link, output logic sampled);
    always @(posedge link.clk) begin
        case (link.valid)
            1'b1: sampled <= link.high[5];
            default: sampled <= 1'b0;
        endcase
    end
endmodule

module interface_flow_tb;
    logic clk;
    logic [7:0] payload, spare;
    logic sampled, unused;
    channel active_link(clk), other_link(clk);
    interface_source tx(active_link, payload), other_tx(other_link, spare);
    interface_sink rx(active_link, sampled), other_rx(other_link, unused);
endmodule
