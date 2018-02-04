#ifndef HALIDE_CODEGEN_FIRRTL_TESTBENCH_H
#define HALIDE_CODEGEN_FIRRTL_TESTBENCH_H

/** \file
 *
 * Defines the code-generator for producing FIRRTL testbench code
 */
#include <sstream>

#include "CodeGen_FIRRTL_Base.h"
#include "CodeGen_FIRRTL_Target.h"
#include "Module.h"
#include "Scope.h"

namespace Halide {

namespace Internal {

/** A code generator that emits Xilinx Vivado FIRRTL compatible C++ testbench code.
 */
class CodeGen_FIRRTL_Testbench : public CodeGen_FIRRTL_Base {
public:
    CodeGen_FIRRTL_Testbench(std::ostream &tb_stream,
                          Target target,
                          OutputKind output_kind);
    ~CodeGen_FIRRTL_Testbench();

protected:
    using CodeGen_FIRRTL_Base::visit;

    void visit(const ProducerConsumer *);
    void visit(const Call *);
    void visit(const Realize *);
    void visit(const Block *);

private:
    CodeGen_FIRRTL_Target cg_target;
};

}
}

#endif
