#include "Halide.h"

using namespace Halide;

Var x("x"), y("y"), c("c");
Var xo("xo"), xi("xi"), yi("yi"), yo("yo");

class MyPipeline {
    ImageParam input;
    ImageParam weight;
    Param<bool> enable;
    Func in;
    Func blur_x, blur_y;
    Func hw_output;
    Func output;
    std::vector<Argument> args;

public:
    MyPipeline() : input(UInt(8), 2, "input"),
                   weight(UInt(8), 2, "weight"),
                   in("in"),
                   blur_x("blur_x"),
                   blur_y("blur_y"),
                   hw_output("hw_output"),
                   output("output") {
        in(x, y) = input(x, y);
        blur_x(x, y) = (in(x, y) * weight(0,0)
                     +  in(x+1, y) * weight(0,1)
                     +  in(x+2, y) * weight(0,2)
                     +  in(x+3, y) * weight(0,3)
                     +  in(x+4, y) * weight(0,4))/5;
        blur_y(x, y) = (blur_x(x, y) * weight(1,0)
                     +  blur_x(x, y+1) * weight(1,1)
                     +  blur_x(x, y+2) * weight(1,2))/3;
        hw_output(x, y) = select(enable==true, blur_y(x, y), in(x, y));
        output(x, y) = hw_output(x, y);

        weight.dim(0).set_bounds(0, 2);
        weight.dim(1).set_bounds(0, 5);
        weight.dim(0).set_stride(1);
        weight.dim(1).set_stride(2);

        args = {input, enable, weight};
    }


    void compile_cpu() {
        std::cout << "\ncompiling cpu code..." << std::endl;

        output.tile(x, y, xo, yo, xi, yi, 256, 256);
        output.bound(x, 0, 256).bound(y, 0, 256);

        output.compile_to_header("pipeline_native.h", args, "pipeline_native");
        output.compile_to_object("pipeline_native.o", args, "pipeline_native");

        std::vector<Target::Feature> features({Target::Zynq});
        Target target(Target::Linux, Target::ARM, 32, features);
        output.compile_to_header("pipeline_arm.h", args, "pipeline_native", target);
        output.compile_to_object("pipeline_arm.o", args, "pipeline_native", target);
    }

    void compile_hls() {
        std::cout << "\ncompiling HLS and FIRRTL code..." << std::endl;

        in.compute_at(output, xo);

        blur_x.compute_at(output, xo);
        blur_x.linebuffer();
        in.fifo_depth(hw_output, 256*2+5+5);

        hw_output.accelerate({in}, xi, xo);
        hw_output.compute_at(output, xo);
        hw_output.tile(x, y, xo, yo, xi, yi, 256, 256);
        hw_output.bound(x, 0, 256).bound(y, 0, 256);

        output.tile(x, y, xo, yo, xi, yi, 256, 256);
        output.bound(x, 0, 256).bound(y, 0, 256);

        Target hls_target = get_target_from_environment();
        hls_target.set_feature(Target::CPlusPlusMangling);
        output.compile_to_hls("pipeline_hls.cpp", args, "pipeline_hls", hls_target);
        output.compile_to_header("pipeline_hls.h", args, "pipeline_hls", hls_target);

        std::vector<Target::Feature> features({Target::Zynq});
        Target target(Target::Linux, Target::ARM, 32, features);
        target.set_feature(Target::CPlusPlusMangling);
        output.compile_to_zynq_c("pipeline_zynq.cpp", args, "pipeline_hls", target);
        output.compile_to_header("pipeline_zynq.h", args, "pipeline_hls", target);

        // FIRRTL generation
        output.compile_to_firrtl("pipeline_firrtl.cpp", args, "pipeline_firrtl", hls_target);
    }
};


int main(int argc, char **argv) {
    MyPipeline p1;
    p1.compile_cpu();

    MyPipeline p2;
    p2.compile_hls();

    return 0;
}
