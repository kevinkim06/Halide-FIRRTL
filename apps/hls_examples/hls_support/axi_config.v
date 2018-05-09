`timescale 1ns/1ps

module axi_config #(
    parameter FILENAME = "param_addr.dat"
) (
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
    output reg done,
    output reg stop_sim
);

integer fp;
integer ret;
integer conf_data;
integer conf_addr;
reg [31:0] r_data;

localparam ADDR_CTRL = 0;
localparam ADDR_STATUS = 4;

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
    stop_sim = 0;
    fp = $fopen(FILENAME, "r");
    @(start);
    while (!$feof(fp))
    begin
        ret = $fscanf(fp, "%x %d\n", conf_addr, conf_data);
        axi_write (conf_addr, conf_data);
    end
    $fclose(fp);
    axi_write (ADDR_CTRL, 1);
    done = 1;
    while (1) begin
        axi_read(ADDR_CTRL, r_data);
        #100;
        @(posedge clk);
        if (r_data==2) begin
            axi_write (ADDR_CTRL, 2);
            stop_sim = 1;
        end
        #100;
        @(posedge clk);
        axi_read(ADDR_STATUS, r_data);
        #100;
        @(posedge clk);
    end
end

endmodule
