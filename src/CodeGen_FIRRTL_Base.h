#ifndef HALIDE_CODEGEN_FIRRTL_BASE_H
#define HALIDE_CODEGEN_FIRRTL_BASE_H

/** \file
 *
 * Defines an base class of the FIRRTL C code-generator
 */
#include "CodeGen_C.h"
#include "Module.h"
#include "Scope.h"

namespace Halide {

namespace Internal {

// FIRRTL_Type : similar to Stencil_Type to include scalar type
//  ---------------------------------------
//   type     constant/stream  scalar/array
//  ---------------------------------------
//  Scalar        constant        scalar
//  Stencil       constant        array
//  Stream        stream          array
//  AxiStream     stream          array
struct FIRRTL_Type {
    typedef enum {Scalar, Stencil, Stream, AxiStream} StencilContainerType;
    StencilContainerType type;
    Type elemType;  // type of the element
    Region bounds;  // extent of each dimension
    int depth;      // FIFO depth if it is a Stream type
    std::vector<int> store_extents; // Store extens. For block code generation.
};

/** This class emits C++ code from given Halide stmt that contains
 * stream and stencil types.
 */
class CodeGen_FIRRTL_Base : public CodeGen_C {
public:
    /** Initialize a C code generator pointing at a particular output
     * stream (e.g. a file, or std::cout) */
    CodeGen_FIRRTL_Base(std::ostream &dest,
                     Target target,
                     OutputKind output_kind,
                     const std::string &include_guard = "")
        : CodeGen_C(dest, target, output_kind, include_guard) {}

    struct Stencil_Type {
        typedef enum {Stencil, Stream, AxiStream} StencilContainerType;
        StencilContainerType type;
        Type elemType;  // type of the element
        Region bounds;  // extent of each dimension
        int depth;      // FIFO depth if it is a Stream type
        std::vector<int> store_extents; // Store extents extracted from stream_subimage and pass in FIRRTL_Argument of add_kernel.
    };

protected:
    Scope<Stencil_Type> stencils;  // scope of stencils and streams of stencils

    virtual std::string print_stencil_type(Stencil_Type s);
    virtual std::string print_name(const std::string &name);
    virtual std::string print_stencil_pragma(const std::string &name);

    using CodeGen_C::visit;

    void visit(const Call *);
    void visit(const Provide *);
    void visit(const Realize *);
};

}
}

#endif
