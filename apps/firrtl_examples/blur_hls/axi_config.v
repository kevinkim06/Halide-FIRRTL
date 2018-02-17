`timescale 1ns/1ps

module axi_config(
    input        clk,
    input        reset,
    output reg [31:0] ARADDR,
    output reg  ARVALID,
    output reg [31:0] AWADDR,
    output reg  AWVALID,
    output reg  BREADY,
    output reg  RREADY,
    output reg [31:0] WDATA,
    output reg [3:0] WSTRB,
    output reg  WVALID,
    input ARREADY,
    input AWREADY,
    input [1:0] BRESP,
    input BVALID,
    input [31:0] RDATA,
    input [1:0] RRESP,
    input RVALID,
    input WREADY,
    input start,
    output reg done
);

reg [31:0] r_data;

localparam ADDR_CTRL = 0;
localparam ADDR_STATUS = 4;
localparam ADDR_ENABLE = 'h40;
localparam ADDR_WEIGHT = 'h44;

task axi_read(
    input [31:0] addr,
    output[31:0] data
);
begin
    ARADDR = addr;
    ARVALID = 1;
    while(ARREADY==0) @(posedge clk);
    @(posedge clk);
    ARVALID = 0;
    RREADY = 1;
    while(RVALID==0) @(posedge clk);
    @(posedge clk);
    data = RDATA;
end
endtask

task axi_write(
    input [31:0] addr,
    input [31:0] data
);
begin
    BREADY = 0;
    AWADDR = addr;
    AWVALID = 1;
    WDATA = data;
    WVALID = 1;
    WSTRB = 15; // TODO
    while(AWREADY==0) @(posedge clk);
    @(posedge clk);
    AWVALID = 0;
    while(WREADY==0) @(posedge clk);
    @(posedge clk);
    WVALID = 0;
    BREADY = 1;
    while(BVALID==0) @(posedge clk);
    @(posedge clk);
    BREADY = 0;
end
endtask

initial begin
    ARADDR = 0;
    ARVALID = 0;
    AWADDR = 0;
    AWVALID = 0;
    BREADY = 0;
    RREADY = 0;
    WDATA = 0;
    WSTRB = 0;
    WVALID = 0;
    done = 0;
    @(start);
    axi_write (ADDR_ENABLE, 1);
    axi_write (ADDR_WEIGHT, 3);
    axi_write (ADDR_WEIGHT+4, 2);
    axi_write (ADDR_WEIGHT+8, 1);
    axi_write (ADDR_WEIGHT+12, 7);
    axi_write (ADDR_WEIGHT+16, 6);
    axi_write (ADDR_WEIGHT+20, 5);
    axi_write (ADDR_WEIGHT+24, 4);
    axi_write (ADDR_WEIGHT+28, 10);
    axi_write (ADDR_WEIGHT+32, 9);
    axi_write (ADDR_WEIGHT+36, 8);
    axi_read (ADDR_ENABLE, r_data);
    axi_read (ADDR_WEIGHT, r_data);
    axi_read (ADDR_WEIGHT+4, r_data);
    axi_read (ADDR_WEIGHT+8, r_data);
    axi_read (ADDR_WEIGHT+12, r_data);
    axi_read (ADDR_WEIGHT+16, r_data);
    axi_read (ADDR_WEIGHT+20, r_data);
    axi_read (ADDR_WEIGHT+24, r_data);
    axi_read (ADDR_WEIGHT+28, r_data);
    axi_read (ADDR_WEIGHT+32, r_data);
    axi_read (ADDR_WEIGHT+36, r_data);
    axi_write (ADDR_CTRL, 1);
    done = 1;
    while (1) begin
        axi_read(ADDR_CTRL, r_data);
        #100;
        @(posedge clk);
        if (r_data==2) axi_write (ADDR_CTRL, 2);
        #100;
        @(posedge clk);
        axi_read(ADDR_STATUS, r_data);
        #100;
        @(posedge clk);
    end
end

endmodule
