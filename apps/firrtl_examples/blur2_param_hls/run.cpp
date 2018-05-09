#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "pipeline_native.h"
#include "pipeline_hls.h"

#include "BufferMinimal.h"
#include "halide_image_io.h"

using Halide::Runtime::HLS::BufferMinimal;
using namespace Halide::Tools;

const unsigned char gaussian2d[5][5] = {
    {1,     3,     6,     3,     1},
    {3,    15,    25,    15,     3},
    {6,    25,    44,    25,     6},
    {3,    15,    25,    15,     3},
    {1,     3,     6,     3,     1}
};

#ifdef ZYNQ
int halide_zynq_init();
#endif

int main(int argc, char **argv) {

#ifdef ZYNQ
    halide_zynq_init();
#endif

    BufferMinimal<uint8_t> in(257, 257);
    BufferMinimal<uint8_t> weight(4, 4);

    BufferMinimal<uint8_t> out_native(256, 256);
    BufferMinimal<uint8_t> out_hls(256, 256);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            in(x, y) = (uint8_t) rand();
        }
    }

    for (int y = 0; y < 4; y++) // just some numbers.
        for (int x = 0; x < 4; x++)
            weight(x, y) = gaussian2d[y][x];

    bool enable = 1;

    printf("start.\n");

    pipeline_native(in, enable, weight, out_native);

    printf("finish running native code\n");

    pipeline_hls(in, enable, weight, out_hls);

    printf("finish running HLS code\n");

    save_png(out_native, "out.png");

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
