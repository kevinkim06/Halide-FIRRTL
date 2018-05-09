#include "Halide.h"

using namespace Halide;

Var x("x"), y("y"), c("c");
Var xo("xo"), xi("xi"), yi("yi"), yo("yo");

class MyPipeline {
    ImageParam input;
    Func in;
    Func blur_x, blur_y;
    Func hw_output;
    Func output;
    std::vector<Argument> args;

public:
    MyPipeline() : input(UInt(8), 2, "input"),
                   in("in"),
                   blur_x("blur_x"),
                   blur_y("blur_y"),
                   hw_output("hw_output"),
                   output("output") {
        in(x, y) = input(x, y);
        blur_x(x, y) = (in(x, y) + in(x+1, y) + in(x+2, y))/3;
        blur_y(x, y) = (blur_x(x, y) + blur_x(x, y+1) + blur_x(x, y+2))/3;
        hw_output(x, y) = blur_y(x, y);
        output(x, y) = hw_output(x, y);
        args = {input};
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
        blur_x.unroll(x, 2).unroll(y, 2);

        hw_output.accelerate({in}, xi, xo);
        hw_output.compute_at(output, xo);
        hw_output.tile(x, y, xo, yo, xi, yi, 256, 256);
        hw_output.bound(x, 0, 256).bound(y, 0, 256);

        output.tile(x, y, xo, yo, xi, yi, 256, 256);
        output.bound(x, 0, 256).bound(y, 0, 256);

        Target hls_target = get_target_from_environment();
        hls_target.set_feature(Target::CPlusPlusMangling);
        hls_target.set_feature(Target::DumpIO);
        output.compile_to_hls("pipeline_hls.cpp", args, "pipeline_hls", hls_target);
        output.compile_to_header("pipeline_hls.h", args, "pipeline_hls", hls_target);

        std::vector<Target::Feature> features({Target::Zynq});
        Target target(Target::Linux, Target::ARM, 32, features);
        target.set_feature(Target::CPlusPlusMangling);
        output.compile_to_zynq_c("pipeline_zynq.cpp", args, "pipeline_hls", target);
        output.compile_to_header("pipeline_zynq.h", args, "pipeline_hls", target);

        // FIRRTL generation
        output.compile_to_firrtl("pipeline_firrtl.v", args, "pipeline_firrtl", hls_target);
    }
};


int main(int argc, char **argv) {
    MyPipeline p1;
    p1.compile_cpu();

    MyPipeline p2;
    p2.compile_hls();

    return 0;
}
