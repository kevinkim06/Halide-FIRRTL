`timescale 1ns/1ps

module instream #(
    parameter IMG_EXTENT_0 = 256,
    parameter IMG_EXTENT_1 = 256,
    parameter IMG_EXTENT_2 = 1,
    parameter IMG_EXTENT_3 = 1,
    parameter ST_EXTENT_0 = 1,
    parameter ST_EXTENT_1 = 1,
    parameter ST_EXTENT_2 = 1,
    parameter ST_EXTENT_3 = 1,
    parameter DATA_SIZE = 8,
    parameter FILENAME = "stream.dat",
    parameter RANDOM_STALL = 1
) (
    input       clk,
    input       reset,
    input       start_in,
    output reg [DATA_SIZE-1:0]tdata[0:ST_EXTENT_3-1][0:ST_EXTENT_2-1][0:ST_EXTENT_1-1][0:ST_EXTENT_0-1],
    output reg  tvalid,
    output reg  tlast,
    input       tready,
    input       stop_in
);



integer seed;
integer idx_0, idx_1, idx_2, idx_3;
integer st_idx_0, st_idx_1, st_idx_2, st_idx_3;
integer stall;
integer stall2;
integer fp;
integer ret;
reg     ready_in;
reg[DATA_SIZE-1:0]data_out_i[0:ST_EXTENT_3-1][0:ST_EXTENT_2-1][0:ST_EXTENT_1-1][0:ST_EXTENT_0-1];
reg     valid_i;

initial begin
    valid_i = 0;
    ready_in = 0;
    fp = $fopen(FILENAME, "r");
    @(posedge start_in);
    @(negedge start_in);

    @(posedge clk);

    for(idx_3 = 0; idx_3 < IMG_EXTENT_3; idx_3 += ST_EXTENT_3)
    for(idx_2 = 0; idx_2 < IMG_EXTENT_2; idx_2 += ST_EXTENT_2)
    for(idx_1 = 0; idx_1 < IMG_EXTENT_1; idx_1 += ST_EXTENT_1)
    for(idx_0 = 0; idx_0 < IMG_EXTENT_0; idx_0 += ST_EXTENT_0)
    begin
        for(st_idx_3 = 0; st_idx_3 < ST_EXTENT_3; st_idx_3++)
        for(st_idx_2 = 0; st_idx_2 < ST_EXTENT_2; st_idx_2++)
        for(st_idx_1 = 0; st_idx_1 < ST_EXTENT_1; st_idx_1++)
        for(st_idx_0 = 0; st_idx_0 < ST_EXTENT_0; st_idx_0++)
        begin
            ret = $fscanf(fp, "%d\n", data_out_i[st_idx_3][st_idx_2][st_idx_1][st_idx_0]);
        end
        if (idx_3 == IMG_EXTENT_3 - ST_EXTENT_3 &&
            idx_2 == IMG_EXTENT_2 - ST_EXTENT_2 &&
            idx_1 == IMG_EXTENT_1 - ST_EXTENT_1 &&
            idx_0 == IMG_EXTENT_0 - ST_EXTENT_0)
        begin
            tlast = 1;
        end
        else
        begin
            tlast = 0;
        end

        if (RANDOM_STALL>0)
        begin
            stall = $random&1;
        end
        else
        begin
            stall = 0;
        end
        if (stall==1) // 1/2 prob: stall
        begin
            valid_i = 0;
            stall2 = 1+$random&31;
            while(stall2>0)
            begin
                stall2 = stall2 - 1;
                @(posedge clk);
            end
            valid_i = 1;
        end
        else // 1/2 prob: not stall
        begin
            valid_i = 1;
        end
        #0.1;
        wait(ready_in);
        @(posedge clk);
    end
    valid_i = 0;
    wait(stop_in);
    $fclose(fp);
end 

always@(*)
  ready_in <= tready;

always@(*) begin
  tdata <= data_out_i;
end

always@(*)
  tvalid <= valid_i;

// Debugging
integer cnt;
always@(posedge clk)
begin
    if (reset)
    begin
        cnt <= 0;
    end
    else if (ready_in&tvalid)
    begin
        cnt <= cnt + 1;
    end
end


endmodule
