#ifndef HALIDE_CODEGEN_FIRRTL_TARGET_H
#define HALIDE_CODEGEN_FIRRTL_TARGET_H

/** \file
 *
 * Defines an IRPrinter that emits FIRRTL code.
 */

#include "CodeGen_FIRRTL_Base.h"
#include "Module.h"
#include "Scope.h"
#include "IRPrinter.h"
#include "Component.h"

namespace Halide {

namespace Internal {

struct FIRRTL_Argument {
    std::string name;

    bool is_output;

    FIRRTL_Type stencil_type;
};


/** This class emits FIRRTL code.
 */
class CodeGen_FIRRTL_Target : public IRPrinter {
public:
    CodeGen_FIRRTL_Target(std::ostream &s, Target t, const std::string &ip_name);

    void add_kernel(Stmt stmt,
                    const std::vector<FIRRTL_Argument> &args);

protected:

    /** An ID for the most recently generated ssa variable */
    std::string id;

    /** The target being generated for. */
    Target target;

    /** A name for the FIRRTL target */
    std::string target_name;

    void open_scope();

    void close_scope(const std::string &);

    void print_module(Component*);
    void print_io(IO*);
    void print_fifo(FIFO*);
    void print_linebuffer(LineBuffer*);
    void print_linebuffer1D(std::string name, int L[4], Type, int inEl[4], int outEl[4]);
    void print_linebuffer2D(std::string name, int L[4], Type, int inEl[4], int outEl[4]);
    void print_linebuffer3D(std::string name, int L[4], Type, int inEl[4], int outEl[4]);
    void print_dispatch(Dispatch*);
    void print_forblock(ForBlock*);
    void print_slaveif(SlaveIf*);
    void generate_firrtl_fifo(int, int);

    struct Reg_Type {
        std::string name;
        bool is_stencil; // scalar or stencil (array)
        int bitwidth; // TODO for packing.
        int range; // bitwidth * extents[0] * extents[1] * ... *extents[3] / 8
        int offset; // address offset in byte
        std::vector<int> extents;
    };

    std::vector<string> for_scanvar_list;
    std::vector<string> for_stencilvar_list;
    std::string producename = ""; // keep last ProcuderConsumer name to be used in ForBlock naming.

    // To keep the pointers to top level and slave interface
    TopLevel * top;
    SlaveIf * sif;

    /** A cache of generated values in scope */
    std::map<std::string, std::string> cache;

    std::string rootName(const std::string &name);
    std::string print_name(const std::string &name);
    std::string print_expr(Expr);
    std::string print_assignment(Type t, const std::string &rhs);
    void print_stmt(Stmt);
    std::string print_base_type(Type);
    std::string print_type(Type);
    std::string print_stencil_type(FIRRTL_Type);

    /** Emit a statement to reinterpret an expression as another type */
    virtual std::string print_reinterpret(Type, Expr);

    // CodeGen for For statements
    int pipeline_depth;
    ForBlock *current_fb;

    using IRPrinter::visit;

    void visit(const Variable *);
    void visit(const IntImm *);
    void visit(const UIntImm *);
    void visit(const StringImm *);
    void visit(const FloatImm *);
    void visit(const Cast *);
    void visit(const Add *);
    void visit(const Sub *);
    void visit(const Mul *);
    void visit(const Div *);
    void visit(const Mod *);
    void visit(const Max *);
    void visit(const Min *);
    void visit(const EQ *);
    void visit(const NE *);
    void visit(const LT *);
    void visit(const LE *);
    void visit(const GT *);
    void visit(const GE *);
    void visit(const And *);
    void visit(const Or *);
    void visit(const Not *);
    void visit(const Call *);
    void visit(const Select *);
    void visit(const Load *);
    void visit(const Store *);
    void visit(const Let *);
    void visit(const LetStmt *);
    void visit(const AssertStmt *);
    void visit(const ProducerConsumer *);

    void visit(const For *);
    void visit(const Provide *);
    void visit(const Allocate *);
    void visit(const Free *);
    void visit(const Realize *);
    void visit(const IfThenElse *);
    void visit(const Evaluate *);

    void visit_uniop(Type t, Expr a, const char *op);
    void visit_binop(Type t, Expr a, Expr b, const char *op);

};

}
}

#endif
