/*
 * Adapter for the original Pocket-side CRT loader.
 *
 * The BIOS still parses supported CRT files into PSRAM as:
 *   0x00000: ROML banks
 *   0x80000: ROMH banks
 *   0x100000: cartridge RAM/register backing area
 *
 * This module recreates the small cartridge mapper that used to live inside
 * the generated MyC64 core and drives the upstream fpga64 cartridge pins.
 */
module legacy_cartridge (
    input  wire        clk,
    input  wire        rst,
    input  wire [1:0]  cart_type,

    input  wire [15:0] addr,
    input  wire [7:0]  data_in,
    input  wire        we,
    input  wire        ioe,
    input  wire        iof,
    input  wire        roml,
    input  wire        romh,

    output reg         exrom,
    output reg         game,

    output reg  [20:0] mem_addr,
    output reg         mem_we,
    output wire [7:0]  mem_data,
    input  wire [7:0]  mem_rdata,

    output wire [7:0]  data_out,
    output reg         io_ext,
    output wire        rom_active
);

  localparam [1:0] CART_NONE          = 2'd0;
  localparam [1:0] CART_MAGIC_DESK    = 2'd1;
  localparam [1:0] CART_EASYFLASH     = 2'd2;
  localparam [1:0] CART_ACTION_REPLAY = 2'd3;

  reg [7:0] reg_de00;
  reg [7:0] reg_de02;

  assign mem_data = data_in;
  assign data_out = mem_rdata;
  assign rom_active = (cart_type != CART_NONE) && (roml || romh);

  always @(posedge clk) begin
    if (rst || cart_type == CART_NONE) begin
      reg_de00 <= 8'h00;
      reg_de02 <= 8'h00;
    end else begin
      case (cart_type)
        CART_MAGIC_DESK: begin
          if (ioe && we && addr[7:0] == 8'h00)
            reg_de00 <= data_in;
        end

        CART_EASYFLASH: begin
          if (ioe && we) begin
            if (addr[7:0] == 8'h00)
              reg_de00 <= data_in;
            if (addr[7:0] == 8'h02)
              reg_de02 <= data_in;
          end
        end

        CART_ACTION_REPLAY: begin
          if (!reg_de00[2] && ioe && we && addr[7:0] == 8'h00)
            reg_de00 <= data_in;
        end

        default: begin
        end
      endcase
    end
  end

  always @* begin
    exrom = 1'b1;
    game = 1'b1;
    mem_addr = 21'h000000;
    mem_we = 1'b0;
    io_ext = 1'b0;

    case (cart_type)
      CART_MAGIC_DESK: begin
        if (!reg_de00[7]) begin
          exrom = 1'b0;
          game = 1'b1;
        end
        mem_addr = {1'b0, reg_de00[6:0], addr[12:0]};
      end

      CART_EASYFLASH: begin
        game = reg_de02[2] ? ~reg_de02[0] : 1'b0;
        exrom = ~reg_de02[1];
        mem_addr = {1'b0, romh, reg_de00[5:0], addr[12:0]};
        if (iof) begin
          mem_addr = 21'h100000 + {13'h0000, addr[7:0]};
          mem_we = we;
          io_ext = 1'b1;
        end
      end

      CART_ACTION_REPLAY: begin
        if (!reg_de00[2]) begin
          exrom = reg_de00[1];
          game = ~reg_de00[0];
        end
        mem_addr = {6'h00, reg_de00[4:3], addr[12:0]};
        if (reg_de00[5] && roml) begin
          mem_addr = 21'h010000 + {8'h00, addr[12:0]};
          mem_we = we;
        end
        if (iof) begin
          mem_addr = 21'h011f00 + {13'h0000, addr[7:0]};
          mem_we = we;
          io_ext = 1'b1;
        end
      end

      default: begin
      end
    endcase
  end

endmodule
