#include <iostream>
#include <limits>

#include "CodeGen_FIRRTL_Base.h"
#include "CodeGen_Internal.h"
#include "Substitute.h"
#include "IROperator.h"
#include "Param.h"
#include "Var.h"
#include "Lerp.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::ostream;
using std::endl;
using std::string;
using std::vector;
using std::ostringstream;
using std::to_string;

string CodeGen_FIRRTL_Base::print_stencil_type(Stencil_Type stencil_type) {
    ostringstream oss;
    // C: Stencil<uint16_t, 1, 1, 1> stencil_var;
    // C: hls::stream<Stencil<uint16_t, 1, 1, 1> > stencil_stream_var;

    switch(stencil_type.type) {
    case Stencil_Type::StencilContainerType::Stencil :
        oss << "Stencil<" << print_type(stencil_type.elemType);

        for(const auto &range : stencil_type.bounds) {
            internal_assert(is_one(simplify(range.min == 0)));
            oss << ", " << range.extent;
        }
        oss << ">";
        break;
    case Stencil_Type::StencilContainerType::Stream :
        oss << "hls::stream<PackedStencil<";
        oss << print_type(stencil_type.elemType);

        for(const auto &range : stencil_type.bounds) {
            internal_assert(is_one(simplify(range.min == 0)));
            oss << ", " << range.extent;
        }
        oss << "> >";
        break;
    case Stencil_Type::StencilContainerType::AxiStream :
        oss << "hls::stream<AxiPackedStencil<";
        oss << print_type(stencil_type.elemType);

        for(const auto &range : stencil_type.bounds) {
            internal_assert(is_one(simplify(range.min == 0)));
            oss << ", " << range.extent;
        }
        oss << "> >";
        break;
    default: internal_error;
    }
    return oss.str();
}

string CodeGen_FIRRTL_Base::print_name(const string &name) {
    ostringstream oss;

    // Prefix an underscore to avoid reserved words (e.g. a variable named "while")
    if (isalpha(name[0])) {
        oss << '_';
    }

    for (size_t i = 0; i < name.size(); i++) {
        // vivado FIRRTL compiler doesn't like '__'
        if (!isalnum(name[i])) {
            oss << "_";
        }
        else oss << name[i];
    }
    return oss.str();
}

string CodeGen_FIRRTL_Base::print_stencil_pragma(const string &name) {
    // nothing is printed by default
    return string();
}

void CodeGen_FIRRTL_Base::visit(const Call *op) {
    if (ends_with(op->name, ".stencil") ||
               ends_with(op->name, ".stencil_update")) {
        ostringstream rhs;
        // IR: out.stencil_update(0, 0, 0)
        // C: out_stencil_update(0, 0, 0)
        vector<string> args_indices(op->args.size());
        for(size_t i = 0; i < op->args.size(); i++)
            args_indices[i] = print_expr(op->args[i]);

        rhs << print_name(op->name) << "(";
        for(size_t i = 0; i < op->args.size(); i++) {
            rhs << args_indices[i];
            if (i != op->args.size() - 1)
                rhs << ", ";
        }
        rhs << ")";

        print_assignment(op->type, rhs.str());
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_FIRRTL_Base::visit(const Realize *op) {
    if (ends_with(op->name, ".stream")) {
        // create a stream type
        internal_assert(op->types.size() == 1);
        allocations.push(op->name, {op->types[0]});
        std::vector<int> store_extents;
        for(size_t i = 0; i < op->bounds.size(); i++) store_extents.push_back(1);// just intialize to default.
        Stencil_Type stream_type({Stencil_Type::StencilContainerType::Stream,
                    op->types[0], op->bounds, 1, store_extents});
        stencils.push(op->name, stream_type);

        // emits the declaration for the stream
        do_indent();
        stream << print_stencil_type(stream_type) << ' ' << print_name(op->name) << ";\n";
        stream << print_stencil_pragma(op->name);

        // traverse down
        op->body.accept(this);

        // We didn't generate free stmt inside for stream type
        allocations.pop(op->name);
        stencils.pop(op->name);

    } else if (ends_with(op->name, ".stencil") ||
               ends_with(op->name, ".stencil_update")) {
        // create a stencil type
        internal_assert(op->types.size() == 1);
        allocations.push(op->name, {op->types[0]});
        std::vector<int> store_extents;
        for(size_t i = 0; i < op->bounds.size(); i++) store_extents.push_back(1);// just intialize to default.
        Stencil_Type stype({Stencil_Type::StencilContainerType::Stencil, op->types[0], op->bounds, 1, store_extents});
        stencils.push(op->name, stype);

        do_indent();
        // Stencil<uint16_t, 1, 1, 1> conv1_stencil_update;
        stream << print_stencil_type(stype) << ' ' << print_name(op->name) << ";\n";
        stream << print_stencil_pragma(op->name);

        op->body.accept(this);

        // We didn't generate free stmt inside for stream type
        allocations.pop(op->name);
        stencils.pop(op->name);
    } else {
        CodeGen_C::visit(op);
    }
}

void CodeGen_FIRRTL_Base::visit(const Provide *op) {
    if (ends_with(op->name, ".stencil") ||
        ends_with(op->name, ".stencil_update")) {
        // IR: buffered.stencil_update(1, 2, 3) =
        // C: buffered_stencil_update(1, 2, 3) =
        vector<string> args_indices(op->args.size());
        for(size_t i = 0; i < op->args.size(); i++)
            args_indices[i] = print_expr(op->args[i]);

        internal_assert(op->values.size() == 1);
        string id_value = print_expr(op->values[0]);

        do_indent();
        stream << print_name(op->name) << "(";

        for(size_t i = 0; i < op->args.size(); i++) {
            stream << args_indices[i];
            if (i != op->args.size() - 1)
                stream << ", ";
        }
        stream << ") = " << id_value << ";\n";

        cache.clear();
    } else {
        CodeGen_C::visit(op);
    }
}

}
}
