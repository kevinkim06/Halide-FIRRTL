#AUTOPILOT_ROOT ?= /cad/xilinx/vivado/2015.4/Vivado_HLS/2015.4
#AUTOPILOT_MACH ?= lnx64

#AUTOPILOT_TOOL := ${AUTOPILOT_ROOT}/${AUTOPILOT_MACH}/tools
#AUTOPILOT_TECH := ${AUTOPILOT_ROOT}/common/technology

#HLS_IFLAG += -I ${AUTOPILOT_ROOT}/include
#HLS_IFLAG += -I ${AUTOPILOT_ROOT}/include/ap_sysc
#HLS_IFLAG += -I ${AUTOPILOT_TOOL}/systemc/include
#HLS_IFLAG += -I ${AUTOPILOT_TOOL}/auto_cc/include

HLS_IFLAG += -I ../hls_support
HLS_IFLAG += -I ../hls_support/xilinx_hls_lib_2015_4

HLS_CXXFLAGS = $(HLS_IFLAG) -DC_TEST
HLS_CXXFLAGS += -Wno-unknown-pragmas -Wno-unused-label -Wno-uninitialized -Wno-literal-suffix -Wno-error=unused-variable -Wno-unused-variable -Wno-error=unused-but-set-variable -Wno-unused-but-set-variable

#### Zynq
# local libpng for ARM.
ZYNQ_CXXFLAGS = $(HLS_CXXFLAGS) -I ../hls_support/libs/png/include -L ../hls_support/libs/png/lib -L ../hls_support/libs/zlib/lib -DZYNQ -DHALIDE_NO_JPEG
# png only for now
# Use -static just for convenience
ZYNQ_LDFLAGS = -lpng $(LDFLAGS) -static
# hf is required.
CROSS_COMPILE ?= arm-linux-gnueabihf-

