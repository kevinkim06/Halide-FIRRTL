#### Halide flags
HALIDE_BIN_PATH := ../../..
HALIDE_SRC_PATH := ../../..
include ../../support/Makefile.inc

#### HLS flags

AUTOPILOT_ROOT := /cad/xilinx/vivado/2015.2/Vivado_HLS/2015.2
AUTOPILOT_MACH := lnx64
AUTOPILOT_TOOL := ${AUTOPILOT_ROOT}/${AUTOPILOT_MACH}/tools
AUTOPILOT_TECH := ${AUTOPILOT_ROOT}/common/technology
IFLAG += -I "${AUTOPILOT_TOOL}/systemc/include"
IFLAG += -I "${AUTOPILOT_ROOT}/include"
IFLAG += -I "${AUTOPILOT_ROOT}/include/ap_sysc"
IFLAG += -I "${AUTOPILOT_TOOL}/auto_cc/include"

IFLAG += -I "../hls_support"

include ../hls_support/Makefile.autopilot


#### end of HLS flags

all: test

pipeline: pipeline.cpp
	$(CXX) $(CXXFLAGS) -Wall $^ $(LDFLAGS) $(LIB_HALIDE) -o $@ -lpthread -ldl -lz -ltinfo -g

pipeline_hls.cpp pipeline_native.o: pipeline
	HL_DEBUG_CODEGEN=0 ./pipeline

run: run.cpp pipeline_hls.cpp pipeline_native.o hls_target.cpp
	$(CXX) $(CXXFLAGS) -Wall  -Wno-unknown-pragmas -Wno-unused-label -g  $^ -lpthread -ldl -o $@

test: run
	./run

clean:
	rm -f pipeline run
	rm -f pipeline_native.h pipeline_native.o
	rm -f pipeline_hls.h pipeline_hls.cpp
	rm -f hls_target.h hls_target.cpp