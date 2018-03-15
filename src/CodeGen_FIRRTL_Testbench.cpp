#include <iostream>
#include <limits>

#include "CodeGen_FIRRTL_Base.h"
#include "CodeGen_FIRRTL_Testbench.h"
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
using std::pair;
using std::map;

class FIRRTL_Closure : public Closure {
public:
    FIRRTL_Closure(Stmt s)  {
        s.accept(this);
    }

    vector<FIRRTL_Argument> arguments(const Scope<FIRRTL_Type> &scope);

protected:
    using Closure::visit;

    string outputname;
    void visit(const ProducerConsumer *);
};

void FIRRTL_Closure::visit(const ProducerConsumer *op) {
    if (op->is_producer && ends_with(op->name, ".stream")) {
        outputname = op->name; // keep update, last one will be output name.
    }
    IRVisitor::visit(op);
}

vector<FIRRTL_Argument> FIRRTL_Closure::arguments(const Scope<FIRRTL_Type> &streams_scope) {
    vector<FIRRTL_Argument> res;
    for (const pair<string, Buffer> &i : buffers) {
        debug(3) << "buffer: " << i.first << " " << i.second.size;
        if (i.second.read) debug(3) << " (read)";
        if (i.second.write) debug(3) << " (write)";
        debug(3) << "\n";
    }
    internal_assert(buffers.empty()) << "we expect no references to buffers in a hw pipeline.\n";
    for (const pair<string, Type> &i : vars) {
        debug(3) << "var: " << i.first << "\n";
        bool is_output = false;
        if(ends_with(i.first, ".stream") ||
           ends_with(i.first, ".stencil") ) {
            FIRRTL_Type stype = streams_scope.get(i.first);
            if (i.first == outputname) is_output = true;
            res.push_back({i.first, is_output, stype});
        } else if (ends_with(i.first, ".stencil_update")) {
            internal_error << "we don't expect to see a stencil_update type in FIRRTL_Closure.\n";
        } else {
            FIRRTL_Type stype;
            stype.type = FIRRTL_Type::StencilContainerType::Scalar;
            stype.elemType = i.second;
            stype.depth = 1;
            // it is a scalar variable
            res.push_back({i.first, is_output, stype});

        }
    }
    return res;
}

namespace {
const string tb_verilog1 =
    "`timescale 1ns/1ps\n"
    "\n"
    "module tb_top;\n"
    "\n"
    "reg clk, reset;\n"
    "reg start_config;\n"
    "reg start_stream;\n"
    "wire stop;\n"
    "wire config_done;\n"
    "\n"
    "initial begin\n"
    "    clk = 0;\n"
    "    forever #5 clk = ~clk;\n"
    "end\n"
    "\n"
    "initial begin\n"
    "    reset = 1;\n"
    "    #15 reset = 0;\n"
    "end\n"
    "\n"
    "initial begin\n"
    "    start_stream = 0;\n"
    "    start_config = 0;\n"
    "    @(posedge clk);\n"
    "    @(posedge clk);\n"
    "    @(posedge clk);\n"
    "    start_config = 1;\n"
    "    @(posedge config_done);\n"
    "    start_stream = 1;\n"
    "    @(posedge clk);\n"
    "    start_stream = 0;\n"
    "end\n"
    "\n"
    "initial begin\n"
    "    @(posedge stop);\n"
    "    #10000;\n"
    "    $finish;\n"
    "end\n"
    "\n"
    "wire [31:0] ARADDR;\n"
    "wire ARVALID;\n"
    "wire [31:0] AWADDR;\n"
    "wire AWVALID;\n"
    "wire BREADY;\n"
    "wire RREADY;\n"
    "wire [31:0] WDATA;\n"
    "wire [3:0] WSTRB;\n"
    "wire WVALID;\n"
    "wire ARREADY;\n"
    "wire AWREADY;\n"
    "wire [1:0] BRESP;\n"
    "wire BVALID;\n"
    "wire [31:0] RDATA;\n"
    "wire [1:0] RRESP;\n"
    "wire RVALID;\n"
    "wire WREADY;\n"
    "\n"
    "axi_config axi_config(\n"
    "    .clk     (clk),\n"
    "    .reset   (reset),\n"
    "    .ARADDR  (ARADDR),\n"
    "    .ARVALID (ARVALID),\n"
    "    .AWADDR  (AWADDR),\n"
    "    .AWVALID (AWVALID),\n"
    "    .BREADY  (BREADY),\n"
    "    .RREADY  (RREADY),\n"
    "    .WDATA   (WDATA),\n"
    "    .WSTRB   (WSTRB),\n"
    "    .WVALID  (WVALID),\n"
    "    .ARREADY (ARREADY),\n"
    "    .AWREADY (AWREADY),\n"
    "    .BRESP   (BRESP),\n"
    "    .BVALID  (BVALID),\n"
    "    .RDATA   (RDATA),\n"
    "    .RRESP   (RRESP),\n"
    "    .RVALID  (RVALID),\n"
    "    .WREADY  (WREADY),\n"
    "    .start   (start_config),\n"
    "    .done    (config_done),\n"
    "    .stop_sim(stop)\n"
    ");\n"
    "\n";
}

CodeGen_FIRRTL_Testbench::CodeGen_FIRRTL_Testbench(ostream &tb_stream, Target target, ostream &firrtl_stream, const string &ip_name)
    : IRPrinter(tb_stream), cg_target(firrtl_stream, target, ip_name) {

    stream << tb_verilog1;
}

CodeGen_FIRRTL_Testbench::~CodeGen_FIRRTL_Testbench() {
    stream << "endmodule";
}

void CodeGen_FIRRTL_Testbench::compile(const Module &input)
{
    for (const auto &f : input.functions()) {
        compile(f);
    }
}

void CodeGen_FIRRTL_Testbench::compile(const LoweredFunc &f)
{
    print(f.body);
}

void CodeGen_FIRRTL_Testbench::compile(const Buffer<> &buffer)
{
}

// Extract root of the name
string CodeGen_FIRRTL_Testbench::rootName(const string &name)
{
    ostringstream oss;

    for (size_t i = 0; i < name.size(); i++) {
        if (name[i]=='.') break;
        oss << name[i];
    }
    return oss.str();
}

string CodeGen_FIRRTL_Testbench::print_name(const string &name) {
    ostringstream oss;

    for (size_t i = 0; i < name.size(); i++) {
        if (!isalnum(name[i])) {
            oss << "_";
        }
        else oss << name[i];
    }
    return oss.str();
}

void CodeGen_FIRRTL_Testbench::visit(const ProducerConsumer *op) {
    if (op->is_producer && starts_with(op->name, "_hls_target.")) {
        Stmt hw_body = op->body;

        debug(1) << "compute the closure for hardware pipeline "
                 << op->name << '\n';
        FIRRTL_Closure c(hw_body);
        vector<FIRRTL_Argument> args = c.arguments(stencils);

        // generate FIRRTL target code using the child code generator
        cg_target.add_kernel(hw_body, args);

        stream << "hls_target DUT(\n";
        stream << "    .clock   (clk),\n";
        stream << "    .reset   (reset),\n";
        // for each inputs/outputs
        for (size_t i = 0; i < args.size(); i++) {
            FIRRTL_Type stype = args[i].stencil_type;
            string stream_port_name = print_name(rootName(args[i].name));
            string stream_name = print_name(args[i].name);

            std::vector<int> stencil_extents;
            for (size_t i = 0; i < stype.bounds.size(); i++) {
                stencil_extents.push_back(*as_const_int(stype.bounds[i].extent));
            }

            for (size_t i = stype.bounds.size(); i < 4; i++) { // make 4 for easy code gen.
                stencil_extents.push_back(1);
            }

            bool is_stream = (args[i].stencil_type.type == FIRRTL_Type::StencilContainerType::AxiStream);
            int stencil_dim = stype.bounds.size();
            if (is_stream) { // is stream (all streams are stream of stencils).
                string idx0, idx1, idx2, idx3;
                for(int idx_3 = 0; idx_3 < stencil_extents[3]; idx_3++) {
                    if (stencil_dim > 3) idx3 = "_" + std::to_string(idx_3);
                    else idx3 = "";
                    for(int idx_2 = 0; idx_2 < stencil_extents[2]; idx_2++) {
                        if (stencil_dim > 2) idx2 = "_" + std::to_string(idx_2);
                        else idx2 = "";
                        for(int idx_1 = 0; idx_1 < stencil_extents[1]; idx_1++) {
                            if (stencil_dim > 1) idx1 = "_" + std::to_string(idx_1);
                            else idx1 = "";
                            for(int idx_0 = 0; idx_0 < stencil_extents[0]; idx_0++) {
                                idx0 = "_" + std::to_string(idx_0);
                                stream << "    ." << stream_port_name << "_TDATA" << idx3 << idx2 << idx1 << idx0 << "(" << stream_name << "_value";
                                stream << "[" << std::to_string(idx_3) << "]";
                                stream << "[" << std::to_string(idx_2) << "]";
                                stream << "[" << std::to_string(idx_1) << "]";
                                stream << "[" << std::to_string(idx_0) << "]),\n";
                            }
                        }
                    }
                }
                stream << "    ." << stream_port_name << "_TVALID(" << stream_name << "_valid),\n";
                stream << "    ." << stream_port_name << "_TREADY(" << stream_name << "_ready),\n";
                stream << "    ." << stream_port_name << "_TLAST(" << stream_name << "_last),\n";
            }
        }
        // AXI Slave If
        stream << "    .ARADDR  (ARADDR),\n";
        stream << "    .ARVALID (ARVALID),\n";
        stream << "    .AWADDR  (AWADDR),\n";
        stream << "    .AWVALID (AWVALID),\n";
        stream << "    .BREADY  (BREADY),\n";
        stream << "    .RREADY  (RREADY),\n";
        stream << "    .WDATA   (WDATA),\n";
        stream << "    .WSTRB   (WSTRB),\n";
        stream << "    .WVALID  (WVALID),\n";
        stream << "    .ARREADY (ARREADY),\n";
        stream << "    .AWREADY (AWREADY),\n";
        stream << "    .BRESP   (BRESP),\n";
        stream << "    .BVALID  (BVALID),\n";
        stream << "    .RDATA   (RDATA),\n";
        stream << "    .RRESP   (RRESP),\n";
        stream << "    .RVALID  (RVALID),\n";
        stream << "    .WREADY  (WREADY)\n";
        stream << ");\n";
        stream << "\n";
    } else {
        IRVisitor::visit(op);
    }
}

void CodeGen_FIRRTL_Testbench::visit(const Call *op) {
    if (op->name == "stream_subimage") {
        std::ostringstream rhs;
        // syntax:
        //   stream_subimage(direction, buffer_var, stream_var, address_of_subimage_origin,
        //                   dim_0_stride, dim_0_extent, ...)
        internal_assert(op->args.size() >= 6 && op->args.size() <= 12);
        const StringImm *direction = op->args[0].as<StringImm>();
        //string a1 = print_expr(op->args[1]);
        //string a2 = print_expr(op->args[2]);
        //string a3 = print_expr(op->args[3]);
        //if (direction->value == "buffer_to_stream") {
        //    rhs << "subimage_to_stream(";
        //} else if (direction->value == "stream_to_buffer") {
        //    rhs << "stream_to_subimage(";
        //} else {
        //    internal_error;
        //}
        //rhs << a1 << ", " << a2 << ", " << a3;
        //for (size_t i = 4; i < op->args.size(); i++) {
        //    rhs << ", " << print_expr(op->args[i]);
        //}
        //rhs <<");\n";

        //do_indent();
        //stream << rhs.str();

        std::vector<int> store_extents;
        // Extract store extents and update stencil information.
        for (size_t i = 4; i < op->args.size(); i+=2) {
            store_extents.push_back(*as_const_int(op->args[i+1]));
        }

        // Let's remember stencil type of streams so that FIRRTL_Closure can extract 
        // store extent correctly.
        const Variable *stream_name_var = op->args[2].as<Variable>();
        FIRRTL_Type stype = stencils.get(stream_name_var->name);
        stype.store_extents = store_extents;
        stencils.push(stream_name_var->name, stype);

        string stream_name = print_name(stream_name_var->name);

        std::vector<int> stencil_extents;
        for (size_t i = 0; i < stype.bounds.size(); i++) {
            stencil_extents.push_back(*as_const_int(stype.bounds[i].extent));
        }

        internal_assert(stencil_extents.size() == store_extents.size());
        for (size_t i = stype.bounds.size(); i < 4; i++) { // make 4 for easy code gen.
            stencil_extents.push_back(1);
            store_extents.push_back(1);
        }

        // Declare wires for streams
        string idx = "";
        idx  = "[0:" + std::to_string(stencil_extents[3]-1) + "]";
        idx += "[0:" + std::to_string(stencil_extents[2]-1) + "]";
        idx += "[0:" + std::to_string(stencil_extents[1]-1) + "]";
        idx += "[0:" + std::to_string(stencil_extents[0]-1) + "]";
        stream << "wire [" << stype.elemType.bits() << "-1:0] " << stream_name << "_value" << idx << ";\n";
        stream << "wire " << stream_name << "_valid;\n";
        stream << "wire " << stream_name << "_ready;\n";
        stream << "wire " << stream_name << "_last;\n";

        if (direction->value == "buffer_to_stream") {
            stream << "instream #(\n";
        } else if (direction->value == "stream_to_buffer") {
            stream << "outstream #(\n";
        } else {
            internal_error;
        }
        stream << "    .IMG_EXTENT_0(" << store_extents[0] << "),\n";
        stream << "    .IMG_EXTENT_1(" << store_extents[1] << "),\n";
        stream << "    .IMG_EXTENT_2(" << store_extents[2] << "),\n";
        stream << "    .IMG_EXTENT_3(" << store_extents[3] << "),\n";
        stream << "    .ST_EXTENT_0(" << stencil_extents[0] << "),\n";
        stream << "    .ST_EXTENT_1(" << stencil_extents[1] << "),\n";
        stream << "    .ST_EXTENT_2(" << stencil_extents[2] << "),\n";
        stream << "    .ST_EXTENT_3(" << stencil_extents[3] << "),\n";
        stream << "    .DATA_SIZE(" << stype.elemType.bits() << "),\n";
        stream << "    .FILENAME(\"" << stream_name << ".dat\"),\n";
        stream << "    .RANDOM_STALL(1) )\n";
        stream << stream_name << " (\n";
        stream << "    .clk(clk),\n";
        stream << "    .reset(reset),\n";
        stream << "    .start_in(start_stream),\n";
        stream << "    .tdata(" << stream_name << "_value),\n";
        stream << "    .tlast(" << stream_name << "_last),\n";
        stream << "    .tvalid(" << stream_name << "_valid),\n";
        stream << "    .tready(" << stream_name << "_ready),\n";
        stream << "    .stop_in(stop)\n";
        stream << ");\n";
        stream << "\n";

    } else {
        IRVisitor::visit(op);
    }
}

void CodeGen_FIRRTL_Testbench::visit(const Realize *op) {
    if (ends_with(op->name, ".stream")) {
        // create a AXI stream type and store it in Scope<> stencils.
        internal_assert(op->types.size() == 1);
        //allocations.push(op->name, {op->types[0]});
        vector<int> store_extents;
        for(size_t i = 0; i < op->bounds.size(); i++) store_extents.push_back(1); // default
        FIRRTL_Type stream_type({FIRRTL_Type::StencilContainerType::AxiStream,
                    op->types[0], op->bounds, 1, store_extents});
        stencils.push(op->name, stream_type);

        // traverse down
        op->body.accept(this);

        // We didn't generate free stmt inside for stream type
        //allocations.pop(op->name);
        stencils.pop(op->name);
    } else if (ends_with(op->name, ".stencil") ||
               ends_with(op->name, ".stencil_update")) {
        // create a stencil type
        internal_assert(op->types.size() == 1);
        //allocations.push(op->name, {op->types[0]});
        std::vector<int> store_extents;
        for(size_t i = 0; i < op->bounds.size(); i++) store_extents.push_back(1);// just intialize to default.
        FIRRTL_Type stype({FIRRTL_Type::StencilContainerType::Stencil, op->types[0], op->bounds, 1, store_extents});
        stencils.push(op->name, stype);

        op->body.accept(this);

        // We didn't generate free stmt inside for stream type
        //allocations.pop(op->name);
        stencils.pop(op->name);
    } else {
        IRPrinter::visit(op);
    }
}

void CodeGen_FIRRTL_Testbench::visit(const IntImm *op) {
}

void CodeGen_FIRRTL_Testbench::visit(const UIntImm *op) {
}

void CodeGen_FIRRTL_Testbench::visit(const FloatImm *op) {
}

void CodeGen_FIRRTL_Testbench::visit(const StringImm *op) {
}

void CodeGen_FIRRTL_Testbench::visit(const Cast *op) {
}

void CodeGen_FIRRTL_Testbench::visit(const Variable *op) {
}

void CodeGen_FIRRTL_Testbench::visit(const Add *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const Sub *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const Mul *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const Div *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const Mod *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const Min *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const Max *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const EQ *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const NE *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const LT *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const LE *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const GT *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const GE *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const And *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const Or *op) {
    print(op->a);
    print(op->b);
}

void CodeGen_FIRRTL_Testbench::visit(const Not *op) {
    print(op->a);
}

void CodeGen_FIRRTL_Testbench::visit(const Select *op) {
    print(op->condition);
    print(op->true_value);
    print(op->false_value);
}

void CodeGen_FIRRTL_Testbench::visit(const Load *op) {
    print(op->index);
    if (!is_one(op->predicate)) {
        print(op->predicate);
    }
}

void CodeGen_FIRRTL_Testbench::visit(const Ramp *op) {
    print(op->base);
    print(op->stride);
}

void CodeGen_FIRRTL_Testbench::visit(const Broadcast *op) {
    print(op->value);
}

void CodeGen_FIRRTL_Testbench::visit(const Let *op) {
    print(op->value);
    print(op->body);
}

void CodeGen_FIRRTL_Testbench::visit(const LetStmt *op) {
    print(op->value);
    print(op->body);
}

void CodeGen_FIRRTL_Testbench::visit(const AssertStmt *op) {
    print(op->condition);
    print(op->message);
}

void CodeGen_FIRRTL_Testbench::visit(const For *op) {
    print(op->min);
    print(op->extent);
    print(op->body);
}

void CodeGen_FIRRTL_Testbench::visit(const Store *op) {
    print(op->index);
    print(op->value);
    if (!is_one(op->predicate)) {
        print(op->predicate);
    }
}

void CodeGen_FIRRTL_Testbench::visit(const Provide *op) {
    //print_list(op->args);
    //print_list(op->values);
}

void CodeGen_FIRRTL_Testbench::visit(const Allocate *op) {
    for (size_t i = 0; i < op->extents.size(); i++) {
        print(op->extents[i]);
    }
    if (!is_one(op->condition)) {
        print(op->condition);
    }
    print(op->body);
}

void CodeGen_FIRRTL_Testbench::visit(const Free *op) {
}

void CodeGen_FIRRTL_Testbench::visit(const Prefetch *op) {
    for (size_t i = 0; i < op->bounds.size(); i++) {
        print(op->bounds[i].min);
        print(op->bounds[i].extent);
    }
}

void CodeGen_FIRRTL_Testbench::visit(const Block *op) {
    print(op->first);
    if (op->rest.defined()) print(op->rest);
}

void CodeGen_FIRRTL_Testbench::visit(const IfThenElse *op) {
    while (1) {
        print(op->then_case);

        if (!op->else_case.defined()) {
            break;
        }

        if (const IfThenElse *nested_if = op->else_case.as<IfThenElse>()) {
            op = nested_if;
        } else {
            print(op->else_case);
            break;
        }
    }
}

void CodeGen_FIRRTL_Testbench::visit(const Evaluate *op) {
    print(op->value);
}

void CodeGen_FIRRTL_Testbench::visit(const Shuffle *op) {
}

}
}
