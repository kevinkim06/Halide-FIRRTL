#include <iostream>
#include <fstream>
#include <limits>
#include <algorithm>

#include "CodeGen_HLS_Target.h"
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
using std::ofstream;

namespace {

class ContainForLoop : public IRVisitor {
    using IRVisitor::visit;
    void visit(const For *op) {
        found = true;
        return;
    }

public:
    bool found;

    ContainForLoop() : found(false) {}
};

bool contain_for_loop(Stmt s) {
    ContainForLoop cfl;
    s.accept(&cfl);
    return cfl.found;
}

}

CodeGen_HLS_Target::CodeGen_HLS_Target(const string &name)
    : target_name(name),
      hdrc(hdr_stream, true),
      srcc(src_stream, false) { }


CodeGen_HLS_Target::~CodeGen_HLS_Target() {
    hdr_stream << "#endif\n";

    // write the header and the source streams into files
    string src_name = target_name + ".cpp";
    string hdr_name = target_name + ".h";
    ofstream src_file(src_name.c_str());
    ofstream hdr_file(hdr_name.c_str());
    src_file << src_stream.str() << endl;
    hdr_file << hdr_stream.str() << endl;
    src_file.close();
    hdr_file.close();
}

namespace {
const string hls_header_includes =
    "#include <assert.h>\n"
    "#include <stdio.h>\n"
    "#include <stdlib.h>\n"
    "#include <hls_stream.h>\n"
    "#include \"Stencil.h\"\n";


const string globals =
    // TODO: this next chunk is copy-pasted from posix_math.cpp. A
    // better solution for the C runtime would be nice.
    "template<typename T> T max(T a, T b) {if (a > b) return a; return b;}\n"
    "template<typename T> T min(T a, T b) {if (a < b) return a; return b;}\n";
}

void CodeGen_HLS_Target::init_module() {
    debug(1) << "CodeGen_HLS_Target::init_module\n";

    // wipe the internal streams
    hdr_stream.str("");
    hdr_stream.clear();
    src_stream.str("");
    src_stream.clear();

    // initialize the header file
    string module_name = "HALIDE_CODEGEN_HLS_TARGET_" + target_name + "_H";
    std::transform(module_name.begin(), module_name.end(), module_name.begin(), toupper);
    hdr_stream << "#ifndef " << module_name << '\n';
    hdr_stream << "#define " << module_name << "\n\n";
    hdr_stream << hls_header_includes << '\n';

    // initialize the source file
    src_stream << "#include \"" << target_name << ".h\"\n\n";
    src_stream << "#include \"Linebuffer.h\"\n\n";
    src_stream << globals << "\n";

}

void CodeGen_HLS_Target::add_kernel(Stmt s,
                                    const string &name,
                                    const vector<HLS_Argument> &args) {
    debug(1) << "CodeGen_HLS_Target::add_kernel " << name << "\n";

    hdrc.add_kernel(s, name, args);
    srcc.add_kernel(s, name, args);
}

void CodeGen_HLS_Target::dump() {
    std::cerr << src_stream.str() << std::endl;
}


string CodeGen_HLS_Target::CodeGen_HLS_C::print_stencil_pragma(const string &name) {
    ostringstream oss;
    internal_assert(stencils.contains(name));
    Stencil_Type stype = stencils.get(name);
    if (stype.type == Stencil_Type::StencilContainerType::Stream) {
        oss << "#pragma HLS STREAM variable=" << print_name(name) << " depth=1\n"
            << "#pragma HLS RESOURCE variable=" << print_name(name) << " core=FIFO_SRL\n\n";
    } else if (stype.type == Stencil_Type::StencilContainerType::Stencil) {
        oss << "#pragma HLS ARRAY_PARTITION variable=" << print_name(name) << ".value complete dim=0\n\n";
    } else {
        internal_error;
    }
    return oss.str();
}


void CodeGen_HLS_Target::CodeGen_HLS_C::add_kernel(Stmt stmt,
                                                   const string &name,
                                                   const vector<HLS_Argument> &args) {
    // Emit the function prototype
    stream << "void " << "p" << print_name(name) << "(\n";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_stencil) {
            CodeGen_HLS_Base::Stencil_Type stype = args[i].stencil_type;
            stream << print_stencil_type(args[i].stencil_type) << " ";
            if (args[i].stencil_type.type == Stencil_Type::StencilContainerType::Stream) {
                stream << "&";  // hls_stream needs to be passed by reference
            }
            stream << print_name(args[i].name);
            allocations.push(args[i].name, {args[i].stencil_type.elemType, "null"});
            stencils.push(args[i].name, args[i].stencil_type);
        } else {
            stream << print_type(args[i].scalar_type) << " " << print_name(args[i].name);
        }

        if (i < args.size()-1) stream << ",\n";
    }

    if (is_header) {
        stream << ");\n";
    } else {
        stream << ")\n";
        open_scope();

        // add HLS pragma at function scope
        stream << "#pragma HLS DATAFLOW\n"
               << "#pragma HLS INLINE region\n"
               << "#pragma HLS INTERFACE s_axilite port=return"
               << " bundle=axilite" << print_name(name) << "\n";
        for (size_t i = 0; i < args.size(); i++) {
            if (args[i].is_stencil) {
                if (ends_with(args[i].name, ".stream")) {
                    // stream arguments use AXI-stream interface
                    stream << "#pragma HLS INTERFACE axis "
                           << "port=" << print_name(args[i].name) << "\n";
                } else {
                    // stencil arguments use AXI-lite interface
                    stream << "#pragma HLS INTERFACE s_axilite "
                           << "port=" << print_name(args[i].name)
                           << " bundle=axilite" << print_name(name) << "\n";
                    stream << "#pragma HLS ARRAY_PARTITION "
                           << "variable=" << print_name(args[i].name) << ".value complete dim=0\n";
                }
            } else {
                // scalar arguments use AXI-lite interface
                stream << "#pragma HLS INTERFACE s_axilite "
                       << "port=" << print_name(args[i].name)
                       << " bundle=axilite" << print_name(name) << "\n";
            }
        }
        stream << "\n";

        // print body
        print(stmt);

        close_scope("kernel hls_target" + print_name(name));
    }
    stream << "\n";

    for (size_t i = 0; i < args.size(); i++) {
        // Remove buffer arguments from allocation scope
        if (args[i].stencil_type.type == Stencil_Type::StencilContainerType::Stream) {
            allocations.pop(args[i].name);
            stencils.pop(args[i].name);
        }
    }
}


void CodeGen_HLS_Target::CodeGen_HLS_C::visit(const For *op) {
    internal_assert(op->for_type == ForType::Serial)
        << "Can only emit serial for loops to HLS C\n";

    string id_min = print_expr(op->min);
    string id_extent = print_expr(op->extent);

    do_indent();
    stream << "for (int "
           << print_name(op->name)
           << " = " << id_min
           << "; "
           << print_name(op->name)
           << " < " << id_min
           << " + " << id_extent
           << "; "
           << print_name(op->name)
           << "++)\n";

    open_scope();
    // add a 'PIPELINE' pragma if it is an innermost loop
    if (!contain_for_loop(op->body)) {
        stream << "#pragma HLS PIPELINE\n";
    }
    op->body.accept(this);
    close_scope("for " + print_name(op->name));
}

}
}