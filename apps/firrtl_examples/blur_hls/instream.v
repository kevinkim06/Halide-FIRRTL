`timescale 1ns/1ps

module instream(
    input       clk,
    input       reset,
    input       start_in,
    output reg [7:0]data_out,
    output reg  valid,
    output reg  last_out,
    input       ready,
    input       stop_in
);

integer seed, i;
integer stall;
integer stall2;
integer fp;
reg[31:0] d;
reg     ready_in;
reg[31:0] data_out_i;
reg     valid_i;

initial begin
    valid_i = 0;
    ready_in = 0;
    fp = $fopen("in.txt", "r");
    @(posedge start_in);
    @(negedge start_in);

    @(posedge clk);

    for(i=0; i< 258*258; i=i+1)
    begin
        $fscanf(fp, "%d ", data_out_i);
        if (i == (258*258-1))
        begin
            last_out = 1;
        end
        else
        begin
            last_out = 0;
        end
        stall = $random&1;
        //stall = 0;
        if (stall==1) // 1/2 prob: stall
        begin
            valid_i = 0;
            stall2 = 1+$random&7;
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
  ready_in <= ready;

always@(*)
  data_out <= data_out_i;

always@(*)
  valid <= valid_i;

integer cnt;
always@(posedge clk)
begin
    if (reset)
    begin
        cnt <= 0;
    end
    else if (ready_in&valid)
    begin
        cnt <= cnt + 1;
    end
end


endmodule
