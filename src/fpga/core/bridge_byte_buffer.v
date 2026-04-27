`default_nettype none

module bridge_byte_buffer #(
    parameter AW = 14
) (
    input  wire              bridge_clk,
    input  wire              bridge_wr,
    input  wire [AW-1:2]     bridge_addr,
    input  wire [31:0]       bridge_din,
    output wire [31:0]       bridge_dout,

    input  wire              clk,
    input  wire [AW-1:0]     addr,
    input  wire [7:0]        din,
    input  wire              we,
    output wire [7:0]        dout
);

  localparam WORD_AW = AW - 2;

  wire [WORD_AW-1:0] word_addr = addr[AW-1:2];
  wire [7:0] lane0_a_dout;
  wire [7:0] lane1_a_dout;
  wire [7:0] lane2_a_dout;
  wire [7:0] lane3_a_dout;
  wire [7:0] lane0_b_dout;
  wire [7:0] lane1_b_dout;
  wire [7:0] lane2_b_dout;
  wire [7:0] lane3_b_dout;

  reg [1:0] read_lane;

  always @(posedge clk) read_lane <= addr[1:0];

  assign bridge_dout = {lane0_a_dout, lane1_a_dout, lane2_a_dout,
                        lane3_a_dout};
  assign dout = (read_lane == 2'd0) ? lane0_b_dout :
                (read_lane == 2'd1) ? lane1_b_dout :
                (read_lane == 2'd2) ? lane2_b_dout : lane3_b_dout;

  bram_block_dp #(.DATA(8), .ADDR(WORD_AW)) u_lane0 (
      .a_clk (bridge_clk),
      .a_wr  (bridge_wr),
      .a_addr(bridge_addr),
      .a_din (bridge_din[31:24]),
      .a_dout(lane0_a_dout),
      .b_clk (clk),
      .b_wr  (we && addr[1:0] == 2'd0),
      .b_addr(word_addr),
      .b_din (din),
      .b_dout(lane0_b_dout)
  );

  bram_block_dp #(.DATA(8), .ADDR(WORD_AW)) u_lane1 (
      .a_clk (bridge_clk),
      .a_wr  (bridge_wr),
      .a_addr(bridge_addr),
      .a_din (bridge_din[23:16]),
      .a_dout(lane1_a_dout),
      .b_clk (clk),
      .b_wr  (we && addr[1:0] == 2'd1),
      .b_addr(word_addr),
      .b_din (din),
      .b_dout(lane1_b_dout)
  );

  bram_block_dp #(.DATA(8), .ADDR(WORD_AW)) u_lane2 (
      .a_clk (bridge_clk),
      .a_wr  (bridge_wr),
      .a_addr(bridge_addr),
      .a_din (bridge_din[15:8]),
      .a_dout(lane2_a_dout),
      .b_clk (clk),
      .b_wr  (we && addr[1:0] == 2'd2),
      .b_addr(word_addr),
      .b_din (din),
      .b_dout(lane2_b_dout)
  );

  bram_block_dp #(.DATA(8), .ADDR(WORD_AW)) u_lane3 (
      .a_clk (bridge_clk),
      .a_wr  (bridge_wr),
      .a_addr(bridge_addr),
      .a_din (bridge_din[7:0]),
      .a_dout(lane3_a_dout),
      .b_clk (clk),
      .b_wr  (we && addr[1:0] == 2'd3),
      .b_addr(word_addr),
      .b_din (din),
      .b_dout(lane3_b_dout)
  );

endmodule

`default_nettype wire
