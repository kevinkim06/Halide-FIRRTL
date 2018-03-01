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

    BufferMinimal<uint8_t> in(260, 258, 3);
    BufferMinimal<uint8_t> in0(260, 258);
    BufferMinimal<uint8_t> in1(260, 258);
    BufferMinimal<uint8_t> in2(260, 258);
    BufferMinimal<uint8_t> weight(2, 5);

    BufferMinimal<uint8_t> out_native(256, 256, 3);
    BufferMinimal<uint8_t> out_native0(256, 256);
    BufferMinimal<uint8_t> out_native1(256, 256);
    BufferMinimal<uint8_t> out_native2(256, 256);
    BufferMinimal<uint8_t> out_hls(256, 256, 3);
    BufferMinimal<uint8_t> out_hls0(256, 256);
    BufferMinimal<uint8_t> out_hls1(256, 256);
    BufferMinimal<uint8_t> out_hls2(256, 256);

    for (int y = 0; y < in.height(); y++) {
        for (int x = 0; x < in.width(); x++) {
            for (int c = 0; c < in.channels(); c++) {
                in(x, y, c) = (uint8_t) rand();
            }
            in0(x, y) = in(x, y, 0);
            in1(x, y) = in(x, y, 1);
            in2(x, y) = in(x, y, 2);
        }
    }

    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 2; x++)
            weight(x, y) = gaussian2d[y][x];

    bool enable = 1;

    printf("start.\n");
    save_txt(in, "in.txt");
    save_txt(in0, "in0.txt");
    save_txt(in1, "in1.txt");
    save_txt(in2, "in2.txt");

    pipeline_native(in, enable, weight, out_native);

    printf("finish running native code\n");

    pipeline_hls(in, enable, weight, out_hls);

    printf("finish running HLS code\n");

    save_png(out_native, "out.png");

    for (int y = 0; y < out_native.height(); y++) {
        for (int x = 0; x < out_native.width(); x++) {
            out_native0(x, y) = out_native(x, y, 0);
            out_native1(x, y) = out_native(x, y, 1);
            out_native2(x, y) = out_native(x, y, 2);
            out_hls0(x, y) = out_hls(x, y, 0);
            out_hls1(x, y) = out_hls(x, y, 1);
            out_hls2(x, y) = out_hls(x, y, 2);
        }
    }

    save_txt(out_native, "out_nav.txt");
    save_txt(out_native0, "out_nav0.txt");
    save_txt(out_native1, "out_nav1.txt");
    save_txt(out_native2, "out_nav2.txt");
    save_txt(out_hls, "out_hls.txt");
    save_txt(out_hls0, "out_hls0.txt");
    save_txt(out_hls1, "out_hls1.txt");
    save_txt(out_hls2, "out_hls2.txt");

    bool success = true;
    for (int y = 0; y < out_hls.height(); y++) {
        for (int x = 0; x < out_hls.width(); x++) {
            for (int c = 0; c < out_hls.channels(); c++) {
                if (out_native(x, y, c) != out_hls(x, y, c)) {
                    printf("Mismatch found: out_native(%d, %d, %d) = %d, "
                           "out_hls(%d, %d, %d) = %d\n",
                           x, y, c, out_native(x, y, c),
                           x, y, c, out_hls(x, y, c));
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
