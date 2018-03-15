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

// FIRRTL_Type : similar to Stencil_Type, scalar type is added.
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



}
}

#endif
