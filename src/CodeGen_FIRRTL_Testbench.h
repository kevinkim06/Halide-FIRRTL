#ifndef HALIDE_CODEGEN_FIRRTL_TESTBENCH_H
#define HALIDE_CODEGEN_FIRRTL_TESTBENCH_H

/** \file
 *
 * Defines the code-generator for producing FIRRTL testbench code
 */
#include <sstream>

#include "IRPrinter.h"
#include "CodeGen_FIRRTL_Base.h"
#include "CodeGen_FIRRTL_Target.h"
#include "Module.h"
#include "Scope.h"

namespace Halide {

namespace Internal {

class CodeGen_FIRRTL_Testbench : public IRPrinter {
public:
    CodeGen_FIRRTL_Testbench(ostream &tb_stream, Target target, std::ostream &firrtl_stream, const string &ip_name);
    ~CodeGen_FIRRTL_Testbench();
    /** Emit the declarations contained in the module as Verilog code. */
    /** The verilog code is standalone. TODO: C/Verilog co-simulation */
    void compile(const Module &module);

protected:

    // TODO Do we need these?
    /** Emit a declaration. */
    // @{
    virtual void compile(const LoweredFunc &func);
    virtual void compile(const Buffer<> &buffer);
    // @}

    Scope<FIRRTL_Type> stencils;  // scope of stencils and streams of stencils

    //using IRPrinter::visit;

    std::string rootName(const std::string &name);
    std::string print_name(const std::string &name);
    void visit(const ProducerConsumer *); // calls Closure and add_kernel() for the DUT
    void visit(const Call *);    // generates testbench using stream input/output information.
    void visit(const Realize *); // collects stencil types

    // To prevent printing out, define functions that doesn't print
    void visit(const IntImm *);
    void visit(const UIntImm *);
    void visit(const FloatImm *);
    void visit(const StringImm *);
    void visit(const Cast *);
    void visit(const Variable *);
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Mul *);
    void visit(const Div *);
    void visit(const Mod *);
    void visit(const Min *);
    void visit(const Max *);
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GT *);
    void visit(const GE *);
    void visit(const And *);
    void visit(const Or *);
    void visit(const Not *);
    void visit(const Select *);
    void visit(const Load *);
    void visit(const Ramp *);
    void visit(const Broadcast *);
    void visit(const Let *);
    void visit(const LetStmt *);
    void visit(const AssertStmt *);
    void visit(const For *);
    void visit(const Store *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Block *);
    void visit(const IfThenElse *);
    void visit(const Evaluate *);
    void visit(const Shuffle *);
    void visit(const Prefetch *);

    CodeGen_FIRRTL_Target cg_target;
};

}
}

#endif
