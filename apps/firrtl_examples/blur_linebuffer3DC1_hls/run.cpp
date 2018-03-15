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

    BufferMinimal<uint8_t> in(1, 260, 258);
    BufferMinimal<uint8_t> in0(260, 258);
    BufferMinimal<uint8_t> in1(260, 258);
    BufferMinimal<uint8_t> in2(260, 258);
    BufferMinimal<uint8_t> weight(2, 5);

    BufferMinimal<uint8_t> out_native(1, 256, 256);
    BufferMinimal<uint8_t> out_native0(256, 256);
    BufferMinimal<uint8_t> out_native1(256, 256);
    BufferMinimal<uint8_t> out_native2(256, 256);
    BufferMinimal<uint8_t> out_hls(1, 256, 256);
    BufferMinimal<uint8_t> out_hls0(256, 256);
    BufferMinimal<uint8_t> out_hls1(256, 256);
    BufferMinimal<uint8_t> out_hls2(256, 256);

    for (int y = 0; y < in.dim(2).extent(); y++) {
        for (int x = 0; x < in.dim(1).extent(); x++) {
            for (int c = 0; c < in.dim(0).extent(); c++) {
                in(c, x, y) = (uint8_t) rand();
            }
            in0(x, y) = in(0, x, y);
        }
    }

    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 2; x++)
            weight(x, y) = gaussian2d[y][x];

    bool enable = 1;

    printf("start.\n");

    pipeline_native(in, enable, weight, out_native);

    printf("finish running native code\n");

    pipeline_hls(in, enable, weight, out_hls);

    printf("finish running HLS code\n");

    save_png(out_native, "out.png");

    for (int y = 0; y < out_native.dim(2).extent(); y++) {
        for (int x = 0; x < out_native.dim(1).extent(); x++) {
            out_native0(x, y) = out_native(0, x, y);
            out_hls0(x, y) = out_hls(0, x, y);
        }
    }

    bool success = true;
    for (int y = 0; y < out_hls.dim(2).extent(); y++) {
        for (int x = 0; x < out_hls.dim(1).extent(); x++) {
            for (int c = 0; c < out_hls.dim(0).extent(); c++) {
                if (out_native(x, y, c) != out_hls(x, y, c)) {
                    printf("Mismatch found: out_native(%d, %d, %d) = %d, "
                           "out_hls(%d, %d, %d) = %d\n",
                           c, x, y, out_native(c, x, y),
                           c, x, y, out_hls(c, x, y));
                    success = false;
                }
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
