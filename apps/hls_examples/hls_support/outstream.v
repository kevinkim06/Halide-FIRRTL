`timescale 1ns/1ps

module outstream #(
    parameter IMG_EXTENT_0 = 256,
    parameter IMG_EXTENT_1 = 256,
    parameter IMG_EXTENT_2 = 1,
    parameter IMG_EXTENT_3 = 1,
    parameter ST_EXTENT_0 = 1,
    parameter ST_EXTENT_1 = 1,
    parameter ST_EXTENT_2 = 1,
    parameter ST_EXTENT_3 = 1,
    parameter DATA_SIZE = 8,
    parameter FILENAME = "outstream.dat",
    parameter RANDOM_STALL = 1
) (
    input       clk,
    input       reset,
    input       start_in,
    input [DATA_SIZE-1:0]tdata[0:ST_EXTENT_3-1][0:ST_EXTENT_2-1][0:ST_EXTENT_1-1][0:ST_EXTENT_0-1],
    input       tvalid,
    input       tlast,
    output reg  tready,
    input       stop_in
);


integer seed;
integer idx_0, idx_1, idx_2, idx_3;
integer st_idx_0, st_idx_1, st_idx_2, st_idx_3;
integer stall;
integer stall2;
integer fp_ref;
integer fp_out;
integer ret;
reg     ready_i;
initial begin
    ready_i = 0;
    fp_ref = $fopen(FILENAME, "r");
    fp_out = $fopen("out_rtl.dat", "w");
    $timeformat(-12, 0, " ps", 20);
    @(posedge start_in);
    @(negedge start_in);

    ready_i = 1;
    for(idx_3 = 0; idx_3 < IMG_EXTENT_3; idx_3 += ST_EXTENT_3)
    for(idx_2 = 0; idx_2 < IMG_EXTENT_2; idx_2 += ST_EXTENT_2)
    for(idx_1 = 0; idx_1 < IMG_EXTENT_1; idx_1 += ST_EXTENT_1)
    for(idx_0 = 0; idx_0 < IMG_EXTENT_0; idx_0 += ST_EXTENT_0)
    begin
        if (RANDOM_STALL>0)
        begin
            stall = $random&1;
        end
        else
        begin
            stall = 0;
        end
        if (stall==1)
        begin
            ready_i = 0;
            stall2 = 1 + $random&31;
            while(stall2>0)
            begin
                stall2 = stall2 - 1;
                @(posedge clk);
            end
            ready_i = 1;
            @(posedge clk);
        end
        else // 1/2 prob: not stall
        begin
            ready_i = 1;
            @(posedge clk);
        end
        while(!tvalid) @(posedge clk);
    end
    ready_i = 0;
    wait(stop_in);
    $fclose(fp_ref);
    $fclose(fp_out);
end 

always@(*)
  tready <= ready_i;

// debug
integer cnt;
reg[DATA_SIZE-1:0] data_out;
always@(posedge clk)
begin
    if (reset)
    begin
        cnt = 0;
    end
    else if (tready&tvalid)
    begin
        cnt = cnt + 1;
        for(st_idx_3 = 0; st_idx_3 < ST_EXTENT_3; st_idx_3++)
        for(st_idx_2 = 0; st_idx_2 < ST_EXTENT_2; st_idx_2++)
        for(st_idx_1 = 0; st_idx_1 < ST_EXTENT_1; st_idx_1++)
        for(st_idx_0 = 0; st_idx_0 < ST_EXTENT_0; st_idx_0++)
        begin
            ret = $fscanf(fp_ref, "%d\n", data_out);
            $fwrite(fp_out, "%d\n", tdata[st_idx_3][st_idx_2][st_idx_1][st_idx_0]);
            if (data_out!==tdata[st_idx_3][st_idx_2][st_idx_1][st_idx_0])
            begin
                $display("Data mismatch at time %t", $time);
            end
        end
    end
end

endmodule
