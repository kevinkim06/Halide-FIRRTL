`timescale 1ns/1ps

module tb_top;

reg clk, reset;
reg start_config;
reg start_stream;
wire stop;
wire config_done;
wire start;

initial begin
    clk = 0;
    forever #5 clk = ~clk;
end

initial begin
    reset = 1;
    #15 reset = 0;
end

initial begin
    start_stream = 0;
    start_config = 0;
    @(posedge clk);
    @(posedge clk);
    @(posedge clk);
    start_config = 1;
    @(posedge config_done);
    start_stream = 1;
    @(posedge clk);
    start_stream = 0;
end

wire [31:0] ARADDR;
wire ARVALID;
wire [31:0] AWADDR;
wire AWVALID;
wire BREADY;
wire RREADY;
wire [31:0] WDATA;
wire [3:0] WSTRB;
wire WVALID;
wire ARREADY;
wire AWREADY;
wire [1:0] BRESP;
wire BVALID;
wire [31:0] RDATA;
wire [1:0] RRESP;
wire RVALID;
wire WREADY;

wire [7:0]  src_data0;
wire [7:0]  src_data1;
wire [7:0]  src_data2;
wire        src_valid;
wire        src_ready;
wire        src_last;
wire [15:0] in_data;
wire        in_valid;
wire        in_ready;
wire [15:0] out_data;
wire        out_valid;
wire        out_ready;
wire [7:0]  dst_data0;
wire [7:0]  dst_data1;
wire [7:0]  dst_data2;
wire        dst_valid;
wire        dst_ready;
wire        dst_last;

axi_config axi_config(
    .clk     (clk),
    .reset   (reset),
    .ARADDR  (ARADDR),
    .ARVALID (ARVALID),
    .AWADDR  (AWADDR),
    .AWVALID (AWVALID),
    .BREADY  (BREADY),
    .RREADY  (RREADY),
    .WDATA   (WDATA),
    .WSTRB   (WSTRB),
    .WVALID  (WVALID),
    .ARREADY (ARREADY),
    .AWREADY (AWREADY),
    .BRESP   (BRESP),
    .BVALID  (BVALID),
    .RDATA   (RDATA),
    .RRESP   (RRESP),
    .RVALID  (RVALID),
    .WREADY  (WREADY),
    .start   (start_config),
    .done    (config_done),
    .stop_sim(stop)
);

instream instream(
    .clk(clk),
    .reset(reset),
    .start_in(start_stream),
    .data_out0(src_data0),
    .last_out(src_last),
    .valid(src_valid),
    .ready(src_ready),
    .stop_in(stop)
);

hls_target DUT(
    .clock   (clk),
    .reset   (reset),
    .ARADDR  (ARADDR),
    .ARVALID (ARVALID),
    .AWADDR  (AWADDR),
    .AWVALID (AWVALID),
    .BREADY  (BREADY),
    .RREADY  (RREADY),
    .WDATA   (WDATA),
    .WSTRB   (WSTRB),
    .WVALID  (WVALID),
    .ARREADY (ARREADY),
    .AWREADY (AWREADY),
    .BRESP   (BRESP),
    .BVALID  (BVALID),
    .RDATA   (RDATA),
    .RRESP   (RRESP),
    .RVALID  (RVALID),
    .WREADY  (WREADY),
    .in_1_TDATA_0_0_0(src_data0),
    .in_1_TVALID(src_valid),
    .in_1_TREADY(src_ready),
    .in_1_TLAST(src_last),
    .hw_output_1_TDATA_0_0_0(dst_data0),
    .hw_output_1_TVALID(dst_valid),
    .hw_output_1_TREADY(dst_ready),
    .hw_output_1_TLAST(dst_last)
);

outstream outstream(
    .clk(clk),
    .reset(reset),
    .start_in(start_stream),
    .data_in0(dst_data0),
    .valid(dst_valid),
    .ready(dst_ready),
    .stop_in(stop)
);


initial begin
    @(posedge stop);
    #10000;
    $finish;
end

endmodule
