#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "pipeline_native.h"
#include "pipeline_hls.h"

#include "BufferMinimal.h"
#include "halide_image_io.h"

using Halide::Runtime::HLS::BufferMinimal;
using namespace Halide::Tools;

#ifdef ZYNQ
int halide_zynq_init();
#endif

int main(int argc, char **argv) {

#ifdef ZYNQ
    halide_zynq_init();
#endif

    BufferMinimal<uint8_t> in(258, 258);

    BufferMinimal<uint8_t> out_native(256, 256);
    BufferMinimal<uint8_t> out_hls(256, 256);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            in(x, y) = (uint8_t) rand();
        }
    }

    printf("start.\n");
    save_txt(in, "in.txt");

    pipeline_native(in, out_native);

    printf("finish running native code\n");

    pipeline_hls(in, out_hls);

    printf("finish running HLS code\n");

    save_png(out_native, "out.png");
    save_txt(out_native, "out.txt");

    bool success = true;
    for (int y = 0; y < out_hls.height(); y++) {
        for (int x = 0; x < out_hls.width(); x++) {
            if (out_native(x, y) != out_hls(x, y)) {
                printf("Mismatch found: out_native(%d, %d) = %d, "
                       "out_hls(%d, %d) = %d\n",
                   x, y, out_native(x, y),
                   x, y, out_hls(x, y));
                success = false;
            }
        }
    }

    if (success) {
        printf("Successed!\n");
        return 0;
    } else {
        printf("Failed!\n");
        return 1;
    }
}
