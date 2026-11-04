module top;
  logic [15:0] a, b;
  leaf #(.WIDTH(16)) u_leaf(.data_i(a), .data_o(b));
endmodule
