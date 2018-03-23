#### Halide flags
HALIDE_BIN_PATH := ../../..
HALIDE_SRC_PATH := ../../..
include ../../support/Makefile.inc

#### HLS flags
include ../hls_support/Makefile.inc
HLS_LOG = vivado_hls.log

#### FIRRTL
FIRRTL_CMD = ${FIRRTL_PATH}/utils/bin/firrtl

.PHONY: all run_hls pre_verilog
all: out.png pre_verilog
run_hls: $(HLS_LOG)
pre_verilog: pipeline_firrtl.v hls_target.v param_addr.dat


pipeline: pipeline.cpp
	$(CXX) $(CXXFLAGS) -Wall -g $^ $(LIB_HALIDE) -o $@ $(LDFLAGS) -ltinfo

pipeline_hls.cpp pipeline_native.o pipeline_zynq.cpp pipeline_arm.o pipeline_firrtl.v hls_target.fir: pipeline
	HL_DEBUG_CODEGEN=0 ./pipeline

hls_target.v: hls_target.fir
	$(FIRRTL_CMD) -tn hls_target -i $^ -o $@

param_addr.dat: param.dat hls_target.fir
	../hls_support/reg2addr.py -m hls_target.fir -v param.dat -o $@

run: run.cpp pipeline_hls.cpp hls_target.cpp pipeline_native.o
	$(CXX) $(CXXFLAGS) -O1 -DNDEBUG $(HLS_CXXFLAGS) -g -Wall -Werror $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

halide_zynq_api_setreg.cpp: hls_prj/solution1/impl/ip/auxiliary.xml
	python ../hls_support/gen_reg_api.py $^

run_zynq: run.cpp pipeline_zynq.cpp pipeline_arm.o halide_zynq_api_setreg.cpp ../hls_support/HalideRuntimeZynq.cpp
	$(CROSS_COMPILE)$(CXX) $(CXXFLAGS) $(ZYNQ_CXXFLAGS) -g -Wall -Werror $^ -o $@ $(ZYNQ_LDFLAGS)

out.png param.dat: run
	./run

$(HLS_LOG) hls_prj/solution1/impl/ip/auxiliary.xml: ../hls_support/run_hls.tcl pipeline_hls.cpp run.cpp
	RUN_PATH=$(realpath ./) \
	RUN_ARGS= \
	vivado_hls -f $< -l $(HLS_LOG)


clean:
	rm -f pipeline run
	rm -f pipeline_native.h pipeline_native.o
	rm -f pipeline_hls.h pipeline_hls.cpp
	rm -f hls_target.h hls_target.cpp
