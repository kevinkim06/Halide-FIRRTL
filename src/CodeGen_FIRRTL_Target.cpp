#include <iostream>
#include <iomanip>
#include <fstream>
#include <limits>
#include <algorithm>

#include "CodeGen_FIRRTL_Target.h"
#include "CodeGen_Internal.h"
#include "Substitute.h"
#include "IRMutator.h"
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

class ContainReadStream : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Call *op) {
        if(op->name == "read_stream") {
            found = true;
        }
        return;
    }

public:
    bool found;

    ContainReadStream() : found(false) {}
};

bool contain_read_stream(Stmt s) {
    ContainReadStream cfl;
    s.accept(&cfl);
    return cfl.found;
}

class ContainWriteStream : public IRVisitor {
    using IRVisitor::visit;
    void visit(const Call *op) {
        if(op->name == "write_stream") {
            found = true;
        }
        return;
    }

public:
    bool found;

    ContainWriteStream() : found(false) {}
};

bool contain_write_stream(Stmt s) {
    ContainWriteStream cfl;
    s.accept(&cfl);
    return cfl.found;
}

}

// Extract Params and tap.stencils used in the For loop to make port of them.
class FIRRTL_For_Closure : public Closure {
public:
    FIRRTL_For_Closure(Stmt s)  {
        s.accept(this);
    }

    vector<string> arguments(void);

protected:
    using Closure::visit;
    void visit(const Call *);

};

void FIRRTL_For_Closure::visit(const Call *op)
{
    // Ignore read_stream and write_stream because they're taken care of 
    // by CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(Call).
    if((op->name != "read_stream") &&
       (op->name != "write_stream")) {
        Closure::visit(op);
    }
}

vector<string> FIRRTL_For_Closure::arguments(void) {
    vector<string> res;
    for (const pair<string, Type> &i : vars) {
        if(ends_with(i.first, ".stream") ) {
            internal_error << "we don't expect to see a stream type in FIRRTL_For_Closure.\n";
        } else if(ends_with(i.first, ".stencil") ) {
            res.push_back({i.first});
        } else if (ends_with(i.first, ".stencil_update")) {
            internal_error << "we don't expect to see a stencil_update type in FIRRTL_For_Closure.\n";
        } else {
            // it is a scalar variable
            res.push_back({i.first});
        }
    }
    return res;
}

CodeGen_FIRRTL_Target::CodeGen_FIRRTL_Target(const string &name, Target target)
    : target_name(name),
      srcfir(src_stream,
           target.with_feature(Target::CPlusPlusMangling)) { }

CodeGen_FIRRTL_Target::~CodeGen_FIRRTL_Target() {
    // write the header and the source streams into files
    string src_name = target_name + ".fir";
    ofstream src_file(src_name.c_str());

    src_file << src_stream.str() << endl;
    src_file.close();
}

void CodeGen_FIRRTL_Target::init_module() {
    debug(1) << "CodeGen_FIRRTL_Target::init_module\n";

    // wipe the internal streams
    src_stream.str("");
    src_stream.clear();

    // initialize the source file
    src_stream << ";Generated FIRRTL\n";
    src_stream << ";Target name: " << target_name << "\n";

}

void CodeGen_FIRRTL_Target::add_kernel(Stmt s,
                                    const string &name,
                                    const vector<FIRRTL_Argument> &args) {
    debug(1) << "CodeGen_FIRRTL_Target::add_kernel " << name << "\n";

    srcfir.add_kernel(s, name, args);
}

void CodeGen_FIRRTL_Target::dump() {
    std::cerr << src_stream.str() << std::endl;
}

// Extract root of the name
string CodeGen_FIRRTL_Target::CodeGen_FIRRTL::rootName(string name)
{
    std::size_t found = name.find(".");
    if (found!=std::string::npos)
        return name.replace(name.begin()+name.find("."),name.end(),"");
    else
        return name;
}

string CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_name(const string &name) {
    ostringstream oss;

    for (size_t i = 0; i < name.size(); i++) {
        if (!isalnum(name[i])) {
            oss << "_";
        }
        else oss << name[i];
    }
    return oss.str();
}

string CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_expr(Expr e) {
    id = "$$ BAD ID $$";
    e.accept(this);
    return id;
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_stmt(Stmt s) {
    s.accept(this);
}

string CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_base_type(const Type type) {
    ostringstream oss;

    if (type.is_uint()) oss << "U";
    else oss << "S";

    oss << "Int";

    return oss.str();
}

string CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_type(const Type type) {
    ostringstream oss;

    if (type.is_uint()) oss << "U";
    else oss << "S";

    oss << "Int<" << type.bits() << ">";

    return oss.str();
}

string CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_stencil_type(FIRRTL_Type stencil_type) {
    ostringstream oss;
    // C: Stencil<uint16_t, 1, 1, 1> stencil_var;
    // FIRRTL: UInt<16>[1][1][1] // TODO?
    // C: hls::stream<Stencil<uint16_t, 1, 1, 1> > stencil_stream_var;
    // FIRRTL: UInt<16>[1][1][1] // TODO?

    switch(stencil_type.type) {
    case FIRRTL_Type::StencilContainerType::Scalar :
        oss << print_type(stencil_type.elemType);
        break;
    case FIRRTL_Type::StencilContainerType::Stencil :
        oss << print_type(stencil_type.elemType);
        for(const auto &range : stencil_type.bounds) {
            internal_assert(is_one(simplify(range.min == 0)));
            oss << "[" << range.extent << "]";
        }
        break;
    case FIRRTL_Type::StencilContainerType::Stream : // TODO? remove?
        oss << "{value : ";
        oss << print_type(stencil_type.elemType);
        for(const auto &range : stencil_type.bounds) {
            internal_assert(is_one(simplify(range.min == 0)));
            oss << "[" << range.extent << "]";
        }
        oss << ", valid : UInt<1>, ";
        oss << "flip ready : UInt<1>}" ;

        break;
    case FIRRTL_Type::StencilContainerType::AxiStream :
        oss << "{TDATA : ";
        oss << print_type(stencil_type.elemType);
        for(const auto &range : stencil_type.bounds) {
            internal_assert(is_one(simplify(range.min == 0)));
            oss << "[" << range.extent << "]";
        }
        oss << ", TVALID : UInt<1>, ";
        oss << "flip TREADY : UInt<1>, " ;
        oss << "TLAST : UInt<1>}"; // AXI-S

        break;
    default: internal_error;
    }
    return oss.str();
}

string CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_assignment(Type t, const std::string &rhs) {

    debug(3) << "CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_assignment rhs=" << rhs << "\n";

    map<string, string>::iterator cached = cache.find(rhs);

    // FIXME: better way? // signed/unsigned...
    FIRRTL_Type wire_type = {FIRRTL_Type::StencilContainerType::Scalar,t,Region(),0,{}};

    if (cached == cache.end()) {
        debug(3) << "CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_assignment cachemiss\n";
        id = unique_name('_');
        if (current_cs!=nullptr) {
            current_cs->addWire(id, wire_type);
            current_cs->addConnect(id, rhs);
        } else {
            top->addWire(id, wire_type);
            top->addConnect(id, rhs);
        }
        cache[rhs] = id;
    } else {
        debug(3) << "CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_assignment cachehit\n";
        id = cached->second;
    }
    return id;
}

string CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_reinterpret(Type type, Expr e) {
    ostringstream oss;
    oss << "as" << print_base_type(type) << "(" << print_expr(e) << ")"; // TODO
    return oss.str();
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::add_kernel(Stmt stmt,
                                                       const string &name,
                                                       const vector<FIRRTL_Argument> &args) {
    // Create Top module
    top = new TopLevel("hls_target");
    sif = new SlaveIf("SlaveIf");
    top->addInstance(static_cast<Component*>(sif));
    top->addConnect(sif->getInstanceName() + ".clock", "clock");
    top->addConnect(sif->getInstanceName() + ".reset", "reset");

    // Create some global wires
    FIRRTL_Type wire_1bit = {FIRRTL_Type::StencilContainerType::Scalar,UInt(1),Region(),0,{}};
    FIRRTL_Type wire_2bit = {FIRRTL_Type::StencilContainerType::Scalar,UInt(2),Region(),0,{}};
    FIRRTL_Type wire_4bit = {FIRRTL_Type::StencilContainerType::Scalar,UInt(4),Region(),0,{}};
    FIRRTL_Type wire_32bit = {FIRRTL_Type::StencilContainerType::Scalar,UInt(32),Region(),0,{}};
    sif->addOutPort("start", wire_1bit);

    // Create Slave Bus Interface (AXIS)
    top->addInPort("AWADDR", wire_32bit); // TODO: optimal width?
    sif->addInPort("AWADDR", wire_32bit); // TODO: optimal width?
    top->addConnect(sif->getInstanceName()+ ".AWADDR", "AWADDR");
    top->addInPort("AWVALID", wire_1bit);
    sif->addInPort("AWVALID", wire_1bit);
    top->addConnect(sif->getInstanceName()+ ".AWVALID", "AWVALID");
    top->addOutPort("AWREADY", wire_1bit);
    sif->addOutPort("AWREADY", wire_1bit);
    top->addConnect("AWREADY", sif->getInstanceName()+ ".AWREADY");
    top->addInPort("WVALID", wire_1bit);
    sif->addInPort("WVALID", wire_1bit);
    top->addConnect(sif->getInstanceName()+ ".WVALID", "WVALID");
    top->addOutPort("WREADY", wire_1bit);
    sif->addOutPort("WREADY", wire_1bit);
    top->addConnect("WREADY", sif->getInstanceName()+ ".WREADY");
    top->addInPort("WDATA", wire_32bit); // supports 32-bit data bus only
    sif->addInPort("WDATA", wire_32bit); // supports 32-bit data bus only
    top->addConnect(sif->getInstanceName()+ ".WDATA", "WDATA");
    top->addInPort("WSTRB", wire_4bit);
    sif->addInPort("WSTRB", wire_4bit);
    top->addConnect(sif->getInstanceName()+ ".WSTRB", "WSTRB");

    top->addInPort("ARADDR", wire_32bit); // TODO: optimal width?
    sif->addInPort("ARADDR", wire_32bit); // TODO: optimal width?
    top->addConnect(sif->getInstanceName()+ ".ARADDR", "ARADDR");
    top->addInPort("ARVALID", wire_1bit);
    sif->addInPort("ARVALID", wire_1bit);
    top->addConnect(sif->getInstanceName()+ ".ARVALID", "ARVALID");
    top->addOutPort("ARREADY", wire_1bit);
    sif->addOutPort("ARREADY", wire_1bit);
    top->addConnect("ARREADY", sif->getInstanceName()+ ".ARREADY");
    top->addOutPort("RVALID", wire_1bit);
    sif->addOutPort("RVALID", wire_1bit);
    top->addConnect("RVALID", sif->getInstanceName()+ ".RVALID");
    top->addInPort("RREADY", wire_1bit);
    sif->addInPort("RREADY", wire_1bit);
    top->addConnect(sif->getInstanceName()+ ".RREADY", "RREADY");
    top->addOutPort("RDATA", wire_32bit); // supports 32-bit data bus only
    sif->addOutPort("RDATA", wire_32bit); // supports 32-bit data bus only
    top->addConnect("RDATA", sif->getInstanceName()+ ".RDATA");
    top->addOutPort("RRESP", wire_2bit);
    sif->addOutPort("RRESP", wire_2bit);
    top->addConnect("RRESP", sif->getInstanceName()+ ".RRESP");

    top->addOutPort("BVALID", wire_1bit);
    sif->addOutPort("BVALID", wire_1bit);
    top->addConnect("BVALID", sif->getInstanceName()+ ".BVALID");
    top->addInPort("BREADY", wire_1bit);
    sif->addInPort("BREADY", wire_1bit);
    top->addConnect(sif->getInstanceName()+ ".BREADY", "BREADY");
    top->addOutPort("BRESP", wire_2bit);
    sif->addOutPort("BRESP", wire_2bit);
    top->addConnect("BRESP", sif->getInstanceName()+ ".BRESP");

    // Process for each input/output.
    for (size_t i = 0; i < args.size(); i++) {
        FIRRTL_Type stype = conv_FIRRTL_Type(args[i]); // convert FIRRTL_Argument -> FIRRTL_Type
        debug(3) << "add_kernel: " << args[i].name << " " << print_stencil_type(stype) << "\n";
        if (args[i].is_stream) { // is stream (all streams are stencil stream).
            internal_assert(args[i].stencil_type.type == CodeGen_FIRRTL_Base::Stencil_Type::StencilContainerType::AxiStream);

            string stream_name = print_name(args[i].name);
            debug(3) << "add_kernel: stream_name " << stream_name << "\n";

            FIRRTL_Type stream_type = stype;
            stream_type.type = FIRRTL_Type::StencilContainerType::Stream; // protocol change from AXIS(TDATA,TVALID,TREADY,TLAST) to Stream(value,valid,ready)
            debug(3) << "stream_type: " << print_stencil_type(stream_type) << "\n";
            if (!args[i].is_output) { // Input IO
                // Create IO component for each input and output
                IO *interface = new IO("IO_" + stream_name, ComponentType::Input);

                // Add to top
                top->addInstance(static_cast<Component*>(interface));

                string arg_name = print_name(rootName(args[i].name)); // Use simple name for input.
                interface->addInput(arg_name, stype); // axi stream
                interface->addOutput(stream_name, stream_type);
                interface->setStoreExtents(stream_type.store_extents);
                top->addInput(arg_name, stype);
                //numInputs++;

                // Connect clock/reset
                top->addConnect(interface->getInstanceName() + ".clock", "clock");
                top->addConnect(interface->getInstanceName() + ".reset", "reset");

                // Connect IO input port
                top->addConnect(interface->getInstanceName() + "." + arg_name, arg_name);                          // IO.data_in <= in

                // Connect IO Start/Done
                string done = "IO_" + stream_name + "_done";
                sif->addInPort(done, wire_1bit);
                interface->addInPort("start_in", wire_1bit);
                interface->addOutPort("done_out", wire_1bit);
                top->addConnect(interface->getInstanceName() + ".start_in", sif->getInstanceName() + ".start");    // IO.start_in <= SIF.start
                top->addConnect(sif->getInstanceName() + "." + done, interface->getInstanceName() + ".done_out");  // SIF.done <= IO.done_out

                // Create FIFO following IO
                FIFO *fifo = new FIFO("FIFO_" + stream_name);
                fifo->addInput("data_in", stream_type); // Use data_in, data_out for port name for re-useability.
                fifo->addOutput("data_out", stream_type);

                // Add to top
                top->addInstance(static_cast<Component*>(fifo));

                // Connect clock/reset
                top->addConnect(fifo->getInstanceName() + ".clock", "clock");
                top->addConnect(fifo->getInstanceName() + ".reset", "reset");

                // Connect FIFO input port
                top->addConnect(fifo->getInstanceName() + ".data_in", interface->getInstanceName() + "." + stream_name);

                // Connect FIFO output port
                top->addWire("wire_" + stream_name, stream_type); // Keep FIFO output as a wire.
                top->addConnect("wire_" + stream_name, fifo->getInstanceName() + ".data_out"); // wire <= FIFO.data_out
            } else { // Output IO
                top->addWire("wire_" + print_name(args[i].name), stream_type);
                // Adding output IO when "write_stream" with more than 2 args are processed.
            }

        } else { // constant scalar or stencil
            string s = print_name(args[i].name);
            sif->addOutPort(s, stype);
            sif->addReg("r_" + s, stype); // TODO offset address?
            top->addWire("wire_" + s, stype);
            top->addConnect("wire_" + s, sif->getInstanceName() + "." + s);
        }
    }

    // initialize
    current_fc = nullptr;
    current_fb = nullptr;
    current_cs = nullptr;
    current_ws = nullptr;

    // Visit body to collect components.
    print(stmt);

    // Print collected component in FIRRTL.

    stream << "circuit " + top->getInstanceName() + " :\n";
    open_scope();

    do_indent();
    stream << "; Top instance " << top->getInstanceName() << "\n";
    print_module(top);
    stream << "; SlaveIf instance " << sif->getInstanceName() << "\n";
    print_slaveif(sif);

    for(auto &c : top->getComponents(ComponentType::Input)) {
        do_indent();
        stream << "; Input instance " << c->getInstanceName() << "\n";
        print_io(static_cast<IO*>(c));
    }
    for(auto &c : top->getComponents(ComponentType::Output)) {
        do_indent();
        stream << "; Output instance " << c->getInstanceName() << "\n";
        print_io(static_cast<IO*>(c));
    }
    for(auto &c : top->getComponents(ComponentType::Fifo)) {
        do_indent();
        stream << "; FIFO instance " << c->getInstanceName() << "\n";
        print_fifo(static_cast<FIFO*>(c));
    }
    for(auto &c : top->getComponents(ComponentType::Linebuffer)) {
        do_indent();
        stream << "; Linebuffer instance " << c->getInstanceName() << "\n";
        print_linebuffer(static_cast<LineBuffer*>(c));
    }
    for(auto &c : top->getComponents(ComponentType::Dispatcher)) {
        do_indent();
        stream << "; Dispatch instance " << c->getInstanceName() << "\n";
        print_dispatch(static_cast<Dispatch*>(c));
    }
    for(auto &c : top->getComponents(ComponentType::Forblock)) {
        do_indent();
        stream << "; ForBlock instance " << c->getInstanceName() << "\n";
        print_module(c);
    }
    for(auto &c : top->getComponents(ComponentType::Forcontrol)) {
        do_indent();
        stream << "; ForControl instance " << c->getInstanceName() << "\n";
        print_forcontrol(static_cast<ForControl*>(c));
    }
    for(auto &c : top->getComponents(ComponentType::Computestage)) {
        do_indent();
        stream << "; ComputeStage instance " << c->getInstanceName() << "\n";
        print_computestage(static_cast<ComputeStage*>(c));
    }
    for(auto &c : top->getComponents(ComponentType::Wrstream)) {
        do_indent();
        stream << "; WrStream instance " << c->getInstanceName() << "\n";
        print_wrstream(static_cast<WrStream*>(c));
    }

    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_module(Component *c)
{
    do_indent();
    stream << "module " << c->getModuleName() << " :\n";
    open_scope();

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";
    for(auto &p : c->getInPorts()) {
        do_indent();
        stream << "input " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << "output " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    // Print instances.
    do_indent();
    stream << "; Instances\n";
    for(auto &p : c->getInstances()) {
        do_indent();
        stream << "inst " << p.first << " of " << p.second << "\n";
    }
    stream << "\n";

    // Print Regs.
    do_indent();
    stream << "; Regs\n";
    for(auto &p : c->getRegs()) {
        do_indent();
        stream << "reg " << p.first << " : " << print_stencil_type(p.second) << ", clock\n";
    }
    stream << "\n";

    // Print Wires.
    do_indent();
    stream << "; Wires\n";
    for(auto &p : c->getWires()) {
        do_indent();
        stream << "wire " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    // Print connections.
    do_indent();
    stream << "; Connections\n";
    map<string, string> cn = c->getConnects();
    for(auto &p : c->getConnectKeys()) {
        do_indent();
        stream << p << " <= " << cn[p] << "\n";
    }
    stream << "\n";
    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_slaveif(SlaveIf *c)
{
    do_indent();
    stream << "module " << c->getModuleName() << " :\n";
    open_scope();

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";

    FIRRTL_Type in_stencil;
    for(auto &p : c->getInPorts()) {
        do_indent();
        stream << "input " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    FIRRTL_Type out_stencil;
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << "output " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    // Offset address assignment
    int offset = 0x40; // Base of config registers
    std::map<int, string> complete_address_map;
    std::map<int, Reg_Type> address_map; // map of vector (name, size)
    for(auto &p : c->getRegs()) {
        FIRRTL_Type s = p.second;
        Reg_Type r;
        r.name = p.first;
        std::vector<int> extents;
        if (s.type == FIRRTL_Type::StencilContainerType::Stencil) { // TODO: pack it in 32-bit word
            // TODO: assert s.bounds.size() <= 4
            // TODO: assert s.bounds.size() >= 1
            extents.push_back(1);
            extents.push_back(1);
            extents.push_back(1);
            extents.push_back(1);
            int bsize = s.bounds.size();
            r.range = 1;
            for(int i = 0 ; i < bsize ; i++) {
                const IntImm *e = s.bounds[i].extent.as<IntImm>();
                extents[i] = e->value;
                r.extents.push_back(e->value);
                r.range *= e->value;
            }
            r.is_stencil = true;
            r.bitwidth = 32; //s.elemType.bits(); // TODO for packing
            //TODO for packing... r.range *= r.bitwidth;
            r.range *= 4; // range in byte
            address_map[offset] = r;
            string regidx0, regidx1, regidx2, regidx3;
            for(int i3 = 0; i3 < extents[3]; i3++) {
                if (bsize == 4) regidx3 = "[" + std::to_string(i3) + "]";
                else regidx3 = "";
                for(int i2 = 0; i2 < extents[2]; i2++) {
                    if (bsize >= 3) regidx2 = "[" + std::to_string(i2) + "]";
                    else regidx2 = "";
                    for(int i1 = 0; i1 < extents[1]; i1++) {
                        if (bsize >= 2) regidx1 = "[" + std::to_string(i1) + "]";
                        else regidx1 = "";
                        for(int i0 = 0; i0 < extents[0]; i0++) {
                            regidx0 = "[" + std::to_string(i0) + "]";
                            complete_address_map[offset] = p.first+regidx3+regidx2+regidx1+regidx0; // reverse-order
                            offset += 4; // TODO: packing
                        }
                    }
                }
            }
        } else {
            r.is_stencil = false;
            r.bitwidth = 32;//s.elemType.bits(); // TODO for packing
            r.range = 4; // range in byte
            complete_address_map[offset] = p.first;
            address_map[offset] = r;
            offset += 4;
        }
    }

    // Body
    do_indent(); stream << ";------------------ Register Map -----------------\n";
    do_indent(); stream << "; 0x00000000 : CTRL\n";
    do_indent(); stream << ";              [0]: Start (Write 1 to start, auto cleared)\n";
    do_indent(); stream << ";              [1]: Done (Set to 1 when all block are done. Write 1 to clear)\n";
    do_indent(); stream << "; 0x00000004 : STATUS (Read-Only)\n";
    do_indent(); stream << ";              [0]: Run (1 indicates running).\n";
    do_indent(); stream << "; 0x00000008 : Interrupt Enable // TODO\n";
    do_indent(); stream << "; 0x0000000C : Interrupt Status // TODO\n";
    do_indent(); stream << "; 0x00000010 : Info0 (Read-Only) // TODO: such as loop count?\n";
    do_indent(); stream << "; 0x00000014 : Info1 (Read-Only)\n";
    do_indent(); stream << "; 0x000000l8 : Info2 (Read-Only)\n";
    do_indent(); stream << "; 0x000000lC : Info3 (Read-Only)\n";
    do_indent(); stream << "; 0x00000020 : Info4 (Read-Only)\n";
    do_indent(); stream << "; 0x00000024 : Info5 (Read-Only)\n";
    do_indent(); stream << "; 0x00000028 : Info6 (Read-Only)\n";
    do_indent(); stream << "; 0x0000002C : Info7 (Read-Only)\n";
    for(auto &p : complete_address_map) { // sort by address
        do_indent();
        stream << "; 0x" << std::hex << std::setw(8) << std::setfill('0') << p.first << " : " << p.second << "\n";
    }
    stream << std::dec;
    stream << "\n";

    do_indent(); stream << "wire ST_AW_IDLE : UInt<2>\n";
    do_indent(); stream << "wire ST_AW_ADDR : UInt<2>\n";
    do_indent(); stream << "wire ST_AW_DATA : UInt<2>\n";
    do_indent(); stream << "wire ST_AR_IDLE : UInt<2>\n";
    do_indent(); stream << "wire ST_AR_ADDR : UInt<2>\n";
    do_indent(); stream << "wire ST_AR_DATA : UInt<2>\n";
    stream << "\n";
    do_indent(); stream << "ST_AW_IDLE <= UInt<2>(0)\n";
    do_indent(); stream << "ST_AW_ADDR <= UInt<2>(1)\n";
    do_indent(); stream << "ST_AW_DATA <= UInt<2>(2)\n";
    do_indent(); stream << "ST_AR_IDLE <= UInt<2>(0)\n";
    do_indent(); stream << "ST_AR_ADDR <= UInt<2>(1)\n";
    do_indent(); stream << "ST_AR_DATA <= UInt<2>(2)\n";
    stream << "\n";
    do_indent(); stream << "wire ADDR_CTRL : UInt<32>\n";
    do_indent(); stream << "wire ADDR_STATUS : UInt<32>\n";
    stream << "\n";
    do_indent(); stream << "ADDR_CTRL <= UInt<32>(0)\n";
    do_indent(); stream << "ADDR_STATUS <= UInt<32>(4)\n";
    stream << "\n";

    do_indent(); stream << "reg  r_aw_cs_fsm : UInt<2>, clock with : (reset => (reset, UInt<2>(0)))\n";
    do_indent(); stream << "wire w_aw_ns_fsm : UInt<2>\n";
    do_indent(); stream << "reg  r_aw_addr : UInt<32>, clock\n";
    do_indent(); stream << "reg  r_ar_cs_fsm : UInt<2>, clock with : (reset => (reset, UInt<2>(0)))\n";
    do_indent(); stream << "wire w_ar_ns_fsm : UInt<2>\n";
    do_indent(); stream << "reg  r_ar_addr : UInt<32>, clock\n";
    do_indent(); stream << "reg  r_rd_data : UInt<32>, clock\n";
    do_indent(); stream << "reg  r_start : UInt<1>, clock with : (reset => (reset, UInt<1>(0)))\n";
    do_indent(); stream << "reg  r_run :   UInt<1>, clock with : (reset => (reset, UInt<1>(0)))\n";
    do_indent(); stream << "reg  r_done :  UInt<1>, clock with : (reset => (reset, UInt<1>(0)))\n";
    vector<string> done_ports;
    for(auto &p : c->getInPorts()) {
        if (ends_with(p.first, "_done")) {// collecting done signals
            done_ports.push_back(p.first);
            do_indent();
            stream << "reg  r_" << p.first << " : UInt<1>, clock with : (reset => (reset, UInt<1>(0)))\n";
        }
    }
    for(auto &p : c->getRegs()) {
        FIRRTL_Type s = p.second;
        if (s.type != FIRRTL_Type::StencilContainerType::Stencil) { // TODO: not supported in FIRRTL?
            do_indent();
            stream << "reg  " << p.first << " : " << print_stencil_type(p.second) << ", clock";
            stream << " with : (reset => (reset, " << print_type(s.elemType) << "(0)))\n";
        } else {
            do_indent();
            stream << "reg  " << p.first << " : " << print_stencil_type(s) << ", clock\n";
            for(size_t i = 0 ; i < s.bounds.size() ; i++) { // index of each array TODO is 16-bit enough?
                do_indent(); stream << "wire w_" << p.first << "_rd_idx" << i << " : UInt<16>\n";
                do_indent(); stream << "wire w_" << p.first << "_wr_idx" << i << " : UInt<16>\n";
            }
        }
    }
    stream << "\n";

    do_indent(); stream << "r_aw_cs_fsm <= w_aw_ns_fsm\n";
    do_indent(); stream << "w_aw_ns_fsm <= r_aw_cs_fsm\n";
    do_indent(); stream << "when eq(r_aw_cs_fsm, ST_AW_IDLE) :\n";
    do_indent(); stream << "  when AWVALID :\n";
    do_indent(); stream << "    w_aw_ns_fsm <= ST_AW_ADDR\n";
    do_indent(); stream << "else when eq(r_aw_cs_fsm, ST_AW_ADDR) :\n";
    do_indent(); stream << "  when WVALID :\n";
    do_indent(); stream << "    w_aw_ns_fsm <= ST_AW_DATA\n";
    do_indent(); stream << "else when eq(r_aw_cs_fsm, ST_AW_DATA) :\n";
    do_indent(); stream << "  when BREADY :\n";
    do_indent(); stream << "    w_aw_ns_fsm <= ST_AW_IDLE\n";
    stream << "\n";

    do_indent(); stream << "when and(AWVALID, AWREADY) :\n";
    do_indent(); stream << "  r_aw_addr <= AWADDR\n";
    stream << "\n";
    do_indent(); stream << "AWREADY <= eq(r_aw_cs_fsm, ST_AW_IDLE)\n";
    do_indent(); stream << "WREADY <= eq(r_aw_cs_fsm, ST_AW_ADDR)\n";
    do_indent(); stream << "BVALID <= eq(r_aw_cs_fsm, ST_AW_DATA)\n";
    do_indent(); stream << "BRESP  <= UInt<1>(0)\n";
    stream << "\n";
    do_indent(); stream << "r_ar_cs_fsm <= w_ar_ns_fsm\n";
    stream << "\n";
    do_indent(); stream << "w_ar_ns_fsm <= r_ar_cs_fsm\n";
    do_indent(); stream << "when eq(r_ar_cs_fsm, ST_AR_IDLE) :\n";
    do_indent(); stream << "  when ARVALID :\n";
    do_indent(); stream << "    w_ar_ns_fsm <= ST_AR_ADDR\n";
    do_indent(); stream << "else when eq(r_ar_cs_fsm, ST_AR_ADDR) :\n";
    do_indent(); stream << "  when RREADY :\n";
    do_indent(); stream << "    w_ar_ns_fsm <= ST_AR_DATA\n";
    do_indent(); stream << "else when eq(r_ar_cs_fsm, ST_AR_DATA) :\n";
    do_indent(); stream << "  w_ar_ns_fsm <= ST_AR_IDLE\n";
    stream << "\n";
    do_indent(); stream << "when and(ARVALID, ARREADY) :\n";
    do_indent(); stream << "  r_ar_addr <= ARADDR\n";
    stream << "\n";
    for(auto &p : address_map) {
        Reg_Type r = p.second;
        if (r.is_stencil) {
            if (r.extents.size()==1) {
                do_indent();
                stream << "w_" << r.name << "_rd_idx0" << " <= asUInt(sub(r_ar_addr, UInt(\"h" << std::hex << p.first << "\")))\n";
                do_indent();
                stream << "w_" << r.name << "_wr_idx0" << " <= asUInt(sub(r_aw_addr, UInt(\"h" << std::hex << p.first << "\")))\n";
                stream << std::dec;
            } else {
                int stride = r.extents[0];
                do_indent();
                stream << "w_" << r.name << "_rd_idx0" << " <= rem(asUInt(sub(r_ar_addr, UInt(\"h" << std::hex << p.first << "\"))), ";
                stream << "UInt(\"h" << std::hex << stride << "\"))\n";
                do_indent();
                stream << "w_" << r.name << "_wr_idx0" << " <= rem(asUInt(sub(r_aw_addr, UInt(\"h" << std::hex << p.first << "\"))), ";
                stream << "UInt(\"h" << std::hex << stride << "\"))\n";
                for(size_t i = 1 ; i < r.extents.size() ; i++) {
                    do_indent();
                    stream << "w_" << r.name << "_rd_idx" << i << " <= div(asUInt(sub(r_ar_addr, UInt(\"h" << std::hex << p.first << "\"))), ";
                    stream << "UInt(\"h" << std::hex << stride << "\"))\n";
                    do_indent();
                    stream << "w_" << r.name << "_wr_idx" << i << " <= div(asUInt(sub(r_aw_addr, UInt(\"h" << std::hex << p.first << "\"))), ";
                    stream << "UInt(\"h" << std::hex << stride << "\"))\n";
                    stride *= r.extents[i];
                }
                stream << std::dec; // TODO: do we need this?
            }
        }
    }
    stream << "\n";
    do_indent(); stream << "ARREADY <= eq(r_ar_cs_fsm, ST_AR_IDLE)\n";
    do_indent(); stream << "RRESP <= UInt<2>(0)\n";
    do_indent(); stream << "RVALID <= eq(r_ar_cs_fsm, ST_AR_DATA)\n";
    stream << "\n";
    do_indent(); stream << "when eq(r_ar_cs_fsm, ST_AR_ADDR) :\n";
    do_indent(); stream << "  when eq(r_ar_addr, ADDR_CTRL) :\n";
    do_indent(); stream << "    r_rd_data <= or(shl(r_done, 1), r_start)\n";
    do_indent(); stream << "  else when eq(r_ar_addr, ADDR_STATUS) :\n";
    do_indent(); stream << "    r_rd_data <= r_run\n";
    for(auto &p : address_map) {
        Reg_Type r = p.second;
        if (r.is_stencil) {
            do_indent();
            stream << "  else when and(geq(r_ar_addr, UInt<32>(\"h" << std::hex << p.first << "\")), ";
            stream << "lt(r_ar_addr, UInt<32>(\"h" << (p.first + r.range) << "\"))) :\n";
            do_indent();
            stream << "    r_rd_data <= asUInt(" << r.name;
            for(int i = r.extents.size()-1 ; i >= 0 ; i--) {
                stream << "[w_" << r.name << "_rd_idx" << i << "]";
            }
            stream << ")\n";
            stream << std::dec;
        } else {
            do_indent();
            stream << "  else when eq(r_ar_addr, UInt<32>(\"h" << std::hex << p.first << "\")) :\n";
            do_indent();
            stream << "    r_rd_data <= asUInt(" << r.name << ")\n";
            stream << std::dec;
        }
    }
    stream << "\n";

    do_indent(); stream << "RDATA <= r_rd_data\n";

    do_indent(); stream << "when and(eq(r_aw_cs_fsm, ST_AW_ADDR), eq(r_aw_addr, ADDR_CTRL)) :\n";
    do_indent(); stream << "  r_start <= WDATA ; bit 0 only\n";
    do_indent(); stream << "else :\n";
    do_indent(); stream << "  r_start <= UInt<1>(0)\n";
    stream << "\n";

    do_indent(); stream << "start <= r_start\n";
    stream << "\n";

    do_indent(); stream << "when r_start :\n";
    do_indent(); stream << "  r_run <= UInt<1>(1)\n";
    do_indent(); stream << "else when r_done :\n";
    do_indent(); stream << "  r_run <= UInt<1>(0)\n";
    stream << "\n";

    for(auto &p : done_ports) {
        do_indent(); stream << "when and(eq(r_aw_cs_fsm, ST_AW_ADDR), and(eq(r_aw_addr, ADDR_CTRL), eq(and(WDATA,UInt<32>(2)), UInt<32>(2)))) :\n";
        do_indent(); stream << "    r_" << p << " <= UInt<1>(0)\n";
        do_indent(); stream << "else when " << p << " :\n";
        do_indent(); stream << "    r_" << p << " <= UInt<1>(1)\n";
        stream << "\n";
    }

    do_indent(); stream << "when and(eq(r_aw_cs_fsm, ST_AW_ADDR), and(eq(r_aw_addr, ADDR_CTRL), eq(and(WDATA,UInt<32>(2)), UInt<32>(2)))) :\n";
    do_indent(); stream << "  r_done <= UInt<1>(0)\n";
    do_indent(); stream << "else when";
    int done_ports_size = done_ports.size();
    for(int i = 0; i < done_ports_size; i++) {
        if (i == (done_ports_size-1)) stream << "r_" << done_ports[i];
        else stream << " and(r_" << done_ports[i] << ", ";
    }
    for(int i = 0; i < done_ports_size-1; i++) {
        stream << ")"; //closing and
    }
    stream << " :\n";
    do_indent(); stream << "  r_done <= UInt<1>(1)\n";
    stream << "\n";

    for(auto &p : address_map) { // print in order of address
        Reg_Type r = p.second;
        FIRRTL_Type s = c->getReg(r.name);
        if (r.is_stencil) {
            do_indent(); stream << "when eq(r_aw_cs_fsm, ST_AW_ADDR) :\n";
            do_indent(); stream << "  when and(geq(r_aw_addr, UInt<32>(\"h" << std::hex << p.first << "\")), ";
            stream << "lt(r_aw_addr, UInt<32>(\"h" << std::hex << (p.first + r.range) << "\"))) :\n";
            do_indent(); stream << "    " << r.name;
            for(int i = r.extents.size()-1 ; i >= 0 ; i--) { // reverse order
                stream << "[w_" << r.name << "_wr_idx" << i << "]";
            }
            stream << " <= as" << print_base_type(s.elemType) << "(WDATA)\n";
        } else {
            do_indent(); stream << "when eq(r_aw_cs_fsm, ST_AW_ADDR) :\n";
            do_indent(); stream << "  when eq(r_aw_addr, UInt<32>(\"h" << std::hex << p.first << "\")) :\n";
            do_indent(); stream << "    " << r.name << " <= as" << print_base_type(s.elemType) << "(WDATA)\n";
        }
        stream << "\n";
    }
    stream << std::dec;
    stream << "\n";

    for(auto &p : c->getRegs()) {
        string s = p.first;
        s.replace(0,2,""); // remove "r_"
        do_indent(); stream << s << " <= " << p.first << "\n";
    }
    stream << "\n";

    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_io(IO *c)
{
    do_indent();
    stream << "module " << c->getModuleName() << " :\n";
    open_scope();

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";

    for(auto &p : c->getInPorts()) {
        do_indent();
        stream << "input " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    FIRRTL_Type out_stencil;
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << "output " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    // generate body
    FIRRTL_Type in_stencil;
    for(auto &p : c->getInputs()) { // only one input
        in_stencil = p.second;
        break;
    }
    do_indent(); stream << "; Parameters:\n";
    if (c->isInputIO()) {
        do_indent(); stream << ";  IO Type= IO_IN\n";
    } else {
        do_indent(); stream << ";  IO Type= IO_OUT\n";
    }
    do_indent(); stream << ";  Type=" << in_stencil.elemType << "\n";
    do_indent(); stream << ";  Bits=" << in_stencil.elemType.bits() << "\n";
    do_indent(); stream << ";  Stencil=";
    for(const auto &range : in_stencil.bounds) {
        stream << "[" << range.extent << "]";
    }
    stream << "\n";
    do_indent(); stream << ";  Image Size=";
    for(const auto &s : in_stencil.store_extents) {
        stream << "[" << s << "]";
    }
    stream << "\n";
    stream << "\n";

    in_stencil.type = FIRRTL_Type::StencilContainerType::Stencil; // stream to stencil

    map<string, FIRRTL_Type> istreams = c->getInputs();
    string istr;
    for(auto &i : istreams) {
        istr = i.first; // TODO assert only one stream
    }
    map<string, FIRRTL_Type> ostreams = c->getOutputs();
    string ostr;
    for(auto &i : ostreams) {
        ostr = i.first; // TODO assert only one stream
    }

    // Body of IO
    int store_extents_size = in_stencil.store_extents.size();
    for(int i = 0; i < store_extents_size; i++) {
        do_indent(); stream << "wire VAR_" << i << "_MIN : UInt<16>\n";
        do_indent(); stream << "wire VAR_" << i << "_MAX : UInt<16>\n";
    }
    do_indent(); stream << "wire ST_IDLE  : UInt<2>\n";
    do_indent(); stream << "wire ST_RUN0  : UInt<2>\n";
    do_indent(); stream << "wire ST_DONE  : UInt<2>\n";
    stream << "\n";
    for(int i = 0; i < store_extents_size; i++) {
        do_indent(); stream << "VAR_" << i << "_MIN <= UInt<16>(0)\n";
        do_indent(); stream << "VAR_" << i << "_MAX <= UInt<16>(" << (in_stencil.store_extents[i] - 1) << ")\n";
    }
    stream << "\n";

    do_indent(); stream << "ST_IDLE   <= UInt<2>(0)\n";
    do_indent(); stream << "ST_RUN0   <= UInt<2>(1)\n";
    do_indent(); stream << "ST_DONE   <= UInt<2>(3)\n";
    stream << "\n";
    do_indent(); stream << "reg  r_cs_fsm : UInt<2>, clock with : (reset => (reset, UInt<2>(0)))\n";
    do_indent(); stream << "wire w_ns_fsm : UInt<2>\n";
    do_indent(); stream << "wire w_done : UInt<1>\n";

    for(int i = 0 ; i < store_extents_size; i++) {
        do_indent(); stream << "reg  r_counter_var_" << i << " : UInt<16>, clock with : (reset => (reset, UInt<16>(0)))\n";
    }
    for(int i = 0 ; i < store_extents_size; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_max : UInt<1>\n";
    }
    for(int i = 0 ; i < store_extents_size; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_min : UInt<1>\n";
    }
    do_indent(); stream << "wire w_counter_all_max : UInt<1>\n";
    do_indent(); stream << "reg  r_data_out : " << print_stencil_type(in_stencil) << ", clock\n";
    do_indent(); stream << "reg  r_valid_out : UInt<1>, clock with : (reset => (reset, UInt<1>(0)))\n";
    stream << "\n";

    do_indent(); stream << "wire ready_in_i : UInt<1>\n";
    do_indent(); stream << "wire valid_in_i : UInt<1>\n";
    do_indent(); stream << "wire data_in_i : " << print_stencil_type(in_stencil) << "\n";
    do_indent(); stream << "wire ready_out_i : UInt<1>\n";
    stream << "\n";

    if (c->isInputIO()) {
        do_indent(); stream << "ready_in_i <= " << ostr << ".ready\n";
        do_indent(); stream << "valid_in_i <= " << istr << ".TVALID\n";
        do_indent(); stream << "data_in_i <= " << istr << ".TDATA\n";
    } else {
        do_indent(); stream << "ready_in_i <= " << ostr << ".TREADY\n";
        do_indent(); stream << "valid_in_i <= " << istr << ".valid\n";
        do_indent(); stream << "data_in_i <= " << istr << ".value\n";
    }

    for(int i = 0 ; i < store_extents_size; i++) {
        do_indent(); stream << "w_counter_var_" << i << "_max <= eq(r_counter_var_" << i << ", VAR_" << i << "_MAX)\n";
    }
    for(int i = 0 ; i < store_extents_size; i++) {
        do_indent(); stream << "w_counter_var_" << i << "_min <= eq(r_counter_var_" << i << ", VAR_" << i << "_MIN)\n";
    }
    do_indent(); stream << "w_counter_all_max <= ";
    for(int i = 0 ; i < store_extents_size; i++) {
        if (i == (store_extents_size-1))// last term
            stream << "w_counter_var_" << i << "_max";
        else
            stream << "and(w_counter_var_" << i << "_max, ";
    }
    for(int i = 0 ; i < store_extents_size-1 ; i++) {
        stream << ")";
    }
    stream << "\n";
    stream << "\n";

    for(int i = 0 ; i < store_extents_size; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_reset : UInt<1>\n";
    }
    for(int i = 0 ; i < store_extents_size; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_inc : UInt<1>\n";
    }
    do_indent(); stream << "wire ready_valid : UInt<1>\n";
    stream << "\n";

    do_indent(); stream << "ready_valid <= and(or(not(r_valid_out), ready_in_i), and(valid_in_i, eq(r_cs_fsm, ST_RUN0)))\n";
    for(int i = 0 ; i < store_extents_size; i++) {
        do_indent(); stream << "w_counter_var_" << i << "_reset <= or(start_in, and(ready_valid, ";

        for(int j = 0 ; j <= i; j++) {
            if (j == i) { // last term
                stream << "w_counter_var_" << j << "_max";
            } else {
                stream << "and(w_counter_var_" << j << "_max, ";
            }
        }
        for(int j = 0 ; j <= i ; j++) { // closing AND
            stream << ")";
        }
        stream << ")\n"; // closing OR
    }
    stream << "\n";

    for(int i = 0 ; i < store_extents_size; i++) {
        if (i==0) {
            do_indent(); stream << "w_counter_var_0_inc <= ready_valid\n";
        } else {
            do_indent(); stream << "w_counter_var_" << i << "_inc <= and(ready_valid, ";
            for(int j = 0 ; j <= i; j++) {
                if (j == i) { // last term
                    stream << "not(w_counter_var_" << j << "_max)";
                } else {
                    stream << "and(w_counter_var_" << j << "_max, ";
                }
            }
            for(int j = 0 ; j < i ; j++) { // closing AND
                stream << ")";
            }
            stream << ")\n"; // closing OR
        }
    }
    stream << "\n";

    // FSM
    do_indent(); stream << "r_cs_fsm <= w_ns_fsm\n";
    do_indent(); stream << "w_ns_fsm <= r_cs_fsm\n";
    do_indent(); stream << "when eq(r_cs_fsm, ST_IDLE) :\n";
    do_indent(); stream << "  when start_in :\n";
    do_indent(); stream << "    w_ns_fsm <= ST_RUN0\n";
    do_indent(); stream << "else when eq(r_cs_fsm, ST_RUN0) :\n";
    if (c->isInputIO()) {
        do_indent(); stream << "  when or(w_done, and(" << istr << ".TLAST, ready_valid)) :\n";
    } else {
        do_indent(); stream << "  when w_done :\n";
    }
    do_indent(); stream << "    w_ns_fsm <= ST_DONE\n";
    do_indent(); stream << "else when eq(r_cs_fsm, ST_DONE) :\n";
    do_indent(); stream << "  w_ns_fsm <= ST_IDLE\n";
    stream << "\n";

    do_indent(); stream << "w_done <= and(w_counter_all_max, ready_valid)\n";
    stream << "\n";

    for(int i = 0 ; i < store_extents_size; i++) {
        do_indent(); stream << "when w_counter_var_" << i << "_reset :\n";
        do_indent(); stream << "  r_counter_var_" << i << " <= VAR_" << i << "_MIN\n";
        do_indent(); stream << "else when w_counter_var_" << i << "_inc :\n";
        do_indent(); stream << "  r_counter_var_" << i << " <= add(r_counter_var_" << i << ", UInt<16>(1))\n";
        stream << "\n";
    }

    do_indent(); stream << "ready_out_i <= or(not(r_valid_out), ready_in_i)\n";
    stream << "\n";

    do_indent(); stream << "when ready_valid :\n";
    do_indent(); stream << "  r_data_out <= data_in_i\n";
    stream << "\n";

    do_indent(); stream << "when ready_valid :\n";
    do_indent(); stream << "  r_valid_out <= UInt<1>(1)\n";
    do_indent(); stream << "else when ready_in_i :\n";
    do_indent(); stream << "  r_valid_out <= UInt<1>(0)\n";
    stream << "\n";

    if (c->isInputIO()) {
        do_indent(); stream << istr << ".TREADY <= ready_out_i\n";
        do_indent(); stream << ostr << ".valid <= r_valid_out\n";
        do_indent(); stream << ostr << ".value <= r_data_out\n";
    } else {
        do_indent(); stream << istr << ".ready <= ready_out_i\n";
        do_indent(); stream << ostr << ".TVALID <= r_valid_out\n";
        do_indent(); stream << ostr << ".TDATA <= r_data_out\n";
        do_indent(); stream << ostr << ".TLAST <= w_done\n";
    }

    do_indent(); stream << "done_out <= eq(r_cs_fsm, ST_DONE)\n";
    stream << "\n";

    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_fifo(FIFO *c)
{
    do_indent();
    stream << "module " << c->getModuleName() << " :\n";
    open_scope();

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";

    for(auto &p : c->getInPorts()) {
        do_indent();
        stream << "input " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << "output " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    // TODO: generate body of the FIFO.
    FIRRTL_Type s;
    for(auto &p : c->getInputs()) { // only one input
        s = p.second;
        break;
    }
    do_indent(); stream << "; Parameters:\n";
    do_indent(); stream << ";  Type=" << s.elemType << "\n";
    do_indent(); stream << ";  Bits=" << s.elemType.bits() << "\n";
    do_indent(); stream << ";  Stencil=";
    for(const auto &range : s.bounds) {
        stream << "[" << range.extent << "]";
    }
    stream << "\n";
    int depth = std::stoi(c->getDepth());
    do_indent(); stream << ";  Depth=" << depth << "\n";
    stream << "\n";

    // generate body
    s.type = FIRRTL_Type::StencilContainerType::Stencil;
    do_indent(); stream << "reg  mem : " << print_stencil_type(s) << "[" << depth+1 << "], clock\n";
    do_indent(); stream << "reg  r_wr_ptr : UInt<16>, clock with : (reset => (reset, UInt<16>(0)))\n";
    do_indent(); stream << "reg  r_rd_ptr : UInt<16>, clock with : (reset => (reset, UInt<16>(0)))\n";
    do_indent(); stream << "reg  r_level : UInt<16>, clock with : (reset => (reset, UInt<16>(0)))\n";
    do_indent(); stream << "wire w_push : UInt<1>\n";
    do_indent(); stream << "wire w_pop : UInt<1>\n";
    do_indent(); stream << "reg  r_empty : UInt<1>, clock with : (reset => (reset, UInt<1>(1)))\n";
    do_indent(); stream << "reg  r_full : UInt<1>, clock with : (reset => (reset, UInt<1>(0)))\n";
    do_indent(); stream << "reg  r_valid_out : UInt<1>, clock with : (reset => (reset, UInt<1>(0)))\n";
    do_indent(); stream << "reg  r_data_out : " << print_stencil_type(s) << ", clock\n";
    stream << "\n";

    do_indent(); stream << "w_push <= and(data_in.valid, not(r_full))\n";
    do_indent(); stream << "w_pop  <= and(or(data_out.ready, not(r_valid_out)), not(r_empty))\n";
    stream << "\n";

    do_indent(); stream << "when w_push :\n";
    do_indent(); stream << "  when lt(r_wr_ptr, UInt<16>(" << depth << ")) :\n";
    do_indent(); stream << "    r_wr_ptr <= add(r_wr_ptr, UInt<16>(1))\n";
    do_indent(); stream << "  else :\n";
    do_indent(); stream << "    r_wr_ptr <= UInt<16>(0)\n";
    stream << "\n";

    do_indent(); stream << "when w_pop :\n";
    do_indent(); stream << "  when lt(r_rd_ptr, UInt<16>(" << depth << ")) :\n";
    do_indent(); stream << "    r_rd_ptr <= add(r_rd_ptr, UInt<16>(1))\n";
    do_indent(); stream << "  else :\n";
    do_indent(); stream << "    r_rd_ptr <= UInt<16>(0)\n";
    stream << "\n";

    do_indent(); stream << "when and(w_push, not(w_pop)) :\n";
    do_indent(); stream << "  r_level <= add(r_level, UInt<16>(1))\n";
    do_indent(); stream << "else when and(not(w_push), w_pop) :\n";
    do_indent(); stream << "  r_level <= asUInt(sub(r_level, UInt<16>(1)))\n";
    stream << "\n";

    do_indent(); stream << "when and(w_push, not(w_pop)) :\n";
    do_indent(); stream << "  r_empty <= UInt<1>(0)\n";
    do_indent(); stream << "else when and(not(w_push), w_pop) :\n";
    do_indent(); stream << "  when eq(r_level, UInt<16>(1)) :\n";
    do_indent(); stream << "    r_empty <= UInt<1>(1)\n";
    do_indent(); stream << "  else :\n";
    do_indent(); stream << "    r_empty <= UInt<1>(0)\n";
    stream << "\n";

    do_indent(); stream << "when and(w_push, not(w_pop)) :\n";
    do_indent(); stream << "  when eq(r_level, UInt<16>(" << depth-1 << ")) :\n";
    do_indent(); stream << "    r_full <= UInt<1>(1)\n";
    do_indent(); stream << "  else :\n";
    do_indent(); stream << "    r_full <= UInt<1>(0)\n";
    do_indent(); stream << "else when and(not(w_push), w_pop) :\n";
    do_indent(); stream << "  r_full <= UInt<1>(0)\n";
    stream << "\n";

    do_indent(); stream << "when w_push :\n";
    do_indent(); stream << "  mem[r_wr_ptr] <= data_in.value\n";
    stream << "\n";

    do_indent(); stream << "when w_pop :\n";
    do_indent(); stream << "  r_data_out <= mem[r_rd_ptr]\n";
    stream << "\n";

    do_indent(); stream << "data_out.value <= r_data_out\n";
    stream << "\n";

    do_indent(); stream << "when w_pop :\n";
    do_indent(); stream << "  r_valid_out <= UInt<1>(1)\n";
    do_indent(); stream << "else when data_out.ready :\n";
    do_indent(); stream << "  r_valid_out <= UInt<1>(0)\n";
    stream << "\n";

    do_indent(); stream << "data_out.valid <= r_valid_out\n";
    stream << "\n";

    do_indent(); stream << "data_in.ready <= not(r_full)\n";
    stream << "\n";

    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_linebuffer(LineBuffer *c)
{
    do_indent();
    stream << "module " << c->getModuleName() << " :\n";
    open_scope();

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";

    for(auto &p : c->getInPorts()) {
        do_indent();
        stream << "input " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << "output " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    // TODO: generate body of the Linebuffer.
    string      in_stream;
    FIRRTL_Type in_stencil;
    for(auto &p : c->getInputs()) { // only one input
        in_stream  = p.first;
        in_stencil = p.second;
        break;
    }
    string      out_stream;
    FIRRTL_Type out_stencil;
    for(auto &p : c->getOutputs()) { // only one output
        out_stream  = p.first;
        out_stencil = p.second;
        break;
    }

    int L[4] = {1, 1, 1, 1};
    int inEl[4] = {1, 1, 1, 1};
    int outEl[4] = {1, 1, 1, 1};

    do_indent(); stream << "; Parameters:\n";
    do_indent(); stream << ";  Type=" << in_stencil.elemType << "\n";
    do_indent(); stream << ";  Bits=" << in_stencil.elemType.bits() << "\n";
    do_indent(); stream << ";  Input Stencil=";
    int dim=0;
    for(const auto &range : in_stencil.bounds) {
        stream << "[" << range.extent << "]";
        const IntImm *int_imm = range.extent.as<IntImm>();
        inEl[dim++] = int_imm->value;
    }
    stream << "\n";
    do_indent(); stream << ";  Output Stencil=";
    dim = 0;
    for(const auto &range : out_stencil.bounds) {
        stream << "[" << range.extent << "]";
        const IntImm *int_imm = range.extent.as<IntImm>();
        outEl[dim++] = int_imm->value;
    }
    stream << "\n";
    do_indent(); stream << ";  Image Size=";
    dim = 0;
    for(const auto &s : c->getStoreExtents()) {
        stream << "[" << s << "]";
        L[dim++] = s;
    }
    stream << "\n";

    int nDim = in_stencil.bounds.size();
    string inS = print_type(in_stencil.elemType) + "[" + std::to_string(inEl[0]) + "][" + std::to_string(inEl[1]) + "][" + std::to_string(inEl[2]) + "][" + std::to_string(inEl[3]) + "]";

    // print wrapper
    do_indent(); stream << "clock is invalid\n";
    do_indent(); stream << "reset is invalid\n";
    do_indent(); stream << in_stream << " is invalid\n";
    do_indent(); stream << out_stream << " is invalid\n";
    do_indent(); stream << in_stream << ".ready <= UInt<1>(0)\n";
    do_indent(); stream << "inst LB_" << out_stream << "_" << nDim << "D of LB_" << out_stream << "_" << nDim << "D\n";
    do_indent(); stream << "LB_" << out_stream << "_" << nDim << "D.io is invalid\n";
    do_indent(); stream << "LB_" << out_stream << "_" << nDim << "D.clock <= clock\n";
    do_indent(); stream << "LB_" << out_stream << "_" << nDim << "D.reset <= reset\n";
    do_indent(); stream << "LB_" << out_stream << "_" << nDim << "D.io.in.valid <= UInt<1>(0)\n";
    do_indent(); stream << "wire _inv : {value : " << inS << "}\n";
    do_indent(); stream << "_inv is invalid\n";
    for (int i3=0; i3<inEl[3]; i3++) {
    for (int i2=0; i2<inEl[2]; i2++) {
    for (int i1=0; i1<inEl[1]; i1++) {
    for (int i0=0; i0<inEl[0]; i0++) {
        do_indent(); stream << "LB_" << out_stream << "_" << nDim << "D.io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "] <= _inv.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
    }
    }
    }
    }
    do_indent(); stream << in_stream << ".ready <= UInt<1>(1)\n";
    do_indent(); stream << "when " << in_stream << ".valid :\n";
    do_indent(); stream << "  LB_" << out_stream << "_" << nDim << "D.io.in.valid <= UInt<1>(1)\n";
    for (int i3=0; i3<inEl[3]; i3++) {
    for (int i2=0; i2<inEl[2]; i2++) {
    for (int i1=0; i1<inEl[1]; i1++) {
    for (int i0=0; i0<inEl[0]; i0++) {
        do_indent(); stream << "  LB_" << out_stream << "_" << nDim << "D.io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "] <= " << in_stream << ".value";
        if (nDim == 1) {
            stream << "[" << i0 << "]\n";
        } else if (nDim == 2) {
            stream << "[" << i1 << "][" << i0 << "]\n";
        } else if (nDim == 3) {
            stream << "[" << i2 << "][" << i1 << "][" << i0 << "]\n";
        } else { // if (nDim == 4) {
            stream << "[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
    }
    }
    }
    }
    do_indent(); stream << "  skip\n";
    do_indent(); stream << in_stream << ".ready <= " << "LB_" << out_stream << "_" << nDim << "D.io.in.ready\n";
    for (int i3=0; i3<outEl[3]; i3++) {
    for (int i2=0; i2<outEl[2]; i2++) {
    for (int i1=0; i1<outEl[1]; i1++) {
    for (int i0=0; i0<outEl[0]; i0++) {
        do_indent(); stream << out_stream << ".value";
        if (nDim == 1) {
            stream << "[" << i0 << "]";
        } else if (nDim == 2) {
            stream << "[" << i1 << "][" << i0 << "]";
        } else if (nDim == 3) {
            stream << "[" << i2 << "][" << i1 << "][" << i0 << "]";
        } else { // if (nDim == 4) {
            stream << "[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]";
        }
        stream << " <= LB_" << out_stream << "_" << nDim << "D.io.out.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
    }
    }
    }
    }
    do_indent(); stream << out_stream << ".valid <= " << "LB_" << out_stream << "_" << nDim << "D.io.out.valid\n";
    do_indent(); stream << "LB_" << out_stream << "_" << nDim << "D.io.out.ready <= " << out_stream << ".ready\n";
    stream << "\n";
    close_scope("");

    if (nDim==1) { // TODO
        // TODO: assert inEl[1] == outEl[1] == 1
        // TODO: assert inEl[2] == outEl[2] == 1
        // TODO: assert inEl[3] == outEl[3] == 1
        print_linebuffer1D(c->getModuleName(),      // Prefix of submodule(s)
                           L, // Image Width, Height
                           in_stencil.elemType,     // Type
                           inEl,                    // In Stencil Width, Height
                           outEl);                  // Out Stencil Widht, Height
    } else if (nDim==2) { 
        // TODO: assert inEl[2] == outEl[2] == 1
        // TODO: assert inEl[3] == outEl[3] == 1
        print_linebuffer2D(c->getModuleName(),      // Prefix of submodule(s)
                           L, // Image Width, Height
                           in_stencil.elemType,     // Type
                           inEl,                    // In Stencil Width, Height
                           outEl);                  // Out Stencil Widht, Height
    } else if (nDim==3) {
        // TODO: assert inEl[3] == outEl[3] == 1
        print_linebuffer3D(c->getModuleName(),      // Prefix of submodule(s)
                           L, // Image Width, Height
                           in_stencil.elemType,     // Type
                           inEl,                    // In Stencil Width, Height
                           outEl);                  // Out Stencil Widht, Height
    } else { //TODO
        do_indent(); stream << "; 4D linebuffer TODO\n";
    }
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_linebuffer1D(string name, int L[4], Type t, int inEl[4], int outEl[4])
{
    // TODO: assertion: require(List(inEl.dims).tail == List(outEl.dims).tail, "Except the first dimension, others should match in input and output stencils")
    do_indent();
    stream << "module " << name << "_1D" << " :\n";
    open_scope();

    string inS = print_type(t) + "[" + std::to_string(inEl[0]) + "][" + std::to_string(inEl[1]) + "][" + std::to_string(inEl[2]) + "][" + std::to_string(inEl[3]) + "]";
    string outS = print_type(t) + "[" + std::to_string(outEl[0]) + "][" + std::to_string(outEl[1]) + "][" + std::to_string(outEl[2]) + "][" + std::to_string(outEl[3]) + "]";

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";
    do_indent();
    stream << "output io : {flip in : {flip ready : UInt<1>, valid : UInt<1>, bits : {value : " << inS << "}}, "
           << "out : {flip ready : UInt<1>, valid : UInt<1>, bits : {value : " << outS << "}}}\n";
    stream << "\n";

    do_indent(); stream << "clock is invalid\n";
    do_indent(); stream << "reset is invalid\n";
    do_indent(); stream << "io is invalid\n";

    if (inEl[0] == outEl[0]) {
        do_indent(); stream << "io.out.bits.value <= io.in.bits.value\n";
        do_indent(); stream << "io.in.ready <= io.out.ready\n";
        do_indent(); stream << "io.out.valid <= io.in.valid\n";
    } else {// TODO: } else if (isOutDimDivisibleByIn(0)) {
        int ratio = outEl[0]/inEl[0];
        int bufL0 = std::max(0,ratio - 1);
        int imgL0 = L[0]/inEl[0];
        int nBit_imgL0 = (int)std::ceil(std::log2((float)imgL0));

        do_indent();
        stream << "reg buffer : {value : " << inS << "}[" << bufL0 << "], clock\n";
        do_indent();
        stream << "reg col : UInt<" << nBit_imgL0 << ">, clock with : (reset => (reset, UInt<" << nBit_imgL0 << ">(0)))\n";
        do_indent();
        stream << "wire outStencil : {value : " << outS << "}\n";
        do_indent();
        stream << "outStencil is invalid\n";
        do_indent();
        stream << "io.out.valid <= UInt<1>(0)\n";
        do_indent(); stream << "wire _inv : {value : " << outS << "}\n";
        do_indent(); stream << "_inv is invalid\n";
        for (int i3=0; i3<outEl[3]; i3++) {
        for (int i2=0; i2<outEl[2]; i2++) {
        for (int i1=0; i1<outEl[1]; i1++) {
        for (int i0=0; i0<outEl[0]; i0++) {
            do_indent(); stream << "io.out.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "] <= _inv.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "io.in.ready <= UInt<1>(1)\n";
        do_indent(); stream << "when io.in.valid :\n";
        do_indent(); stream << "  when geq(col, UInt<" << nBit_imgL0 << ">(" << bufL0 << ")) :\n";
        for (int bi=0; bi<bufL0; bi++) {
            int inSliceL0 =  bi * inEl[0];
            for (int i3=0; i3<inEl[3]; i3++) {
            for (int i2=0; i2<inEl[2]; i2++) {
            for (int i1=0; i1<inEl[1]; i1++) {
            for (int i0=0; i0<inEl[0]; i0++) {
                do_indent();
                stream << "    outStencil.value[" << i3 << "][" << i2 << "][" << i1 << "][" << inSliceL0 + i0 << "]"
                          " <= buffer[" << bi << "].value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
            }
            }
            }
            }
        }
        for (int i3=0; i3<inEl[3]; i3++) {
        for (int i2=0; i2<inEl[2]; i2++) {
        for (int i1=0; i1<inEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent();
            stream << "    outStencil.value[" << i3 << "][" << i2 << "][" << i1 << "][" << bufL0*inEl[0] + i0 << "]"
                      " <= io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "    io.out.valid <= UInt<1>(1)\n";
        for (int i3=0; i3<outEl[3]; i3++) {
        for (int i2=0; i2<outEl[2]; i2++) {
        for (int i1=0; i1<outEl[1]; i1++) {
        for (int i0=0; i0<outEl[0]; i0++) {
            do_indent();
            stream << "    io.out.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]"
                      " <= outStencil.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "    skip\n";
        do_indent(); stream << "  when io.out.ready :\n";
        for (int bi=0; bi<bufL0-1; bi++) {
            for (int i3=0; i3<inEl[3]; i3++) {
            for (int i2=0; i2<inEl[2]; i2++) {
            for (int i1=0; i1<inEl[1]; i1++) {
            for (int i0=0; i0<inEl[0]; i0++) {
                do_indent();
                stream << "    buffer[" << bi << "].value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]"
                          " <= buffer[" << bi+1 << "].value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
            }
            }
            }
            }
        }
        for (int i3=0; i3<inEl[3]; i3++) {
        for (int i2=0; i2<inEl[2]; i2++) {
        for (int i1=0; i1<inEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent();
            stream << "    buffer[" << bufL0-1 << "].value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]"
                      " <= io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "    node col_is_max = eq(col, UInt<" << nBit_imgL0 << ">(" << imgL0-1 << "))\n";
        do_indent(); stream << "    node col_inc = tail(add(col, UInt<1>(1)), 1)\n";
        do_indent(); stream << "    col <= col_inc\n";
        do_indent(); stream << "    when col_is_max :\n";
        do_indent(); stream << "      col <= UInt<1>(0)\n";
        do_indent(); stream << "      skip\n";
        do_indent(); stream << "    skip\n";
        do_indent(); stream << "  skip\n";
        do_indent(); stream << "io.in.ready <= io.out.ready\n";
    }

    stream << "\n";
    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_linebuffer2D(string name, int L[4], Type t, int inEl[4], int outEl[4])
{
    // TODO: require(isOutDimDivisibleByIn(1))
    // TODO: require(inEl.dim(2) == outEl.dim(2))
    // TODO: require(inEl.dim(3) == outEl.dim(3))

    do_indent();
    stream << "module " << name << "_2D" << " :\n";
    open_scope();

    string inS = print_type(t) + "[" + std::to_string(inEl[0]) + "][" + std::to_string(inEl[1]) + "][" + std::to_string(inEl[2]) + "][" + std::to_string(inEl[3]) + "]";
    string outS = print_type(t) + "[" + std::to_string(outEl[0]) + "][" + std::to_string(outEl[1]) + "][" + std::to_string(outEl[2]) + "][" + std::to_string(outEl[3]) + "]";
    string l1S = print_type(t) + "[" + std::to_string(inEl[0]) + "][" + std::to_string(outEl[1]) + "][" + std::to_string(outEl[2]) + "][" + std::to_string(outEl[3]) + "]";

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";
    do_indent();
    stream << "output io : {flip in : {flip ready : UInt<1>, valid : UInt<1>, bits : {value : " << inS << "}}, "
           << "out : {flip ready : UInt<1>, valid : UInt<1>, bits : {value : " << outS << "}}}\n";
    stream << "\n";

    do_indent(); stream << "clock is invalid\n";
    do_indent(); stream << "reset is invalid\n";
    do_indent(); stream << "io is invalid\n";

    if ((inEl[0] == outEl[0])&&(inEl[1] == outEl[1])) {
        do_indent(); stream << "io.in.ready <= UInt<1>(0)\n";
        for (int i3=0; i3<inEl[3]; i3++) {
        for (int i2=0; i2<inEl[2]; i2++) {
        for (int i1=0; i1<inEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent(); stream << "io.out.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "] <= io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "io.in.ready <= io.out.ready\n";
        do_indent(); stream << "io.out.valid <= io.in.valid\n";
    } else { //} else if(isOutDimDivisibleByIn(0) && isOutDimDivisibleByIn(1)) {
        // TODO: require(L0 > inEl.dim(0) && L0 > outEl.dim(0))
        // TODO: require(L0 % inEl.dim(0) == 0)

        int ratio1 = outEl[1]/inEl[1];
        int bufL0 = L[0]/inEl[0];
        int bufL1 = ratio1 - 1;
        int imgL0 = L[0]/inEl[0];
        int imgL1 = L[1]/inEl[1];
        int nBit_imgL0 = (int)std::ceil(std::log2((float)imgL0));
        int nBit_imgL1 = (int)std::ceil(std::log2((float)imgL1));
        int nBit_bufL1 = std::max(1, (int)std::ceil(std::log2((float)bufL1)));
        int nBit_inEl1 = (int)std::ceil(std::log2((float)inEl[1]));
        int nBit_outEl1 = (int)std::ceil(std::log2((float)outEl[1]));

        do_indent(); stream << "io.in.ready <= UInt<1>(0)\n";
        do_indent(); stream << "reg col : UInt<" << nBit_imgL0 << ">, clock with : (reset => (reset, UInt<" << nBit_imgL0 << ">(0)))\n";
        do_indent(); stream << "reg row : UInt<" << nBit_imgL1 << ">, clock with : (reset => (reset, UInt<" << nBit_imgL1 << ">(0)))\n";
        do_indent(); stream << "reg rowModBufL1 : UInt<" << nBit_bufL1 << ">, clock with : (reset => (reset, UInt<" << nBit_bufL1 << ">(0)))\n";
        for(int i=0; i<bufL1; i++) {
            do_indent(); stream << "cmem buffer" << i << " : {value : " << inS << "}[" << bufL0 << "]\n";
        }
        do_indent(); stream << "wire slice : {value : " << l1S << "}\n";
        do_indent(); stream << "slice is invalid\n";
        do_indent(); stream << "inst " << name << "_1D of " << name << "_1D" << "\n";
        do_indent(); stream << name << "_1D.io is invalid\n";
        do_indent(); stream << name << "_1D.clock <= clock\n";
        do_indent(); stream << name << "_1D.reset <= reset\n";
        do_indent(); stream << name << "_1D.io.in.valid <= UInt<1>(0)\n";
        do_indent(); stream << "wire _inv : {value : " << l1S << "}\n";
        do_indent(); stream << "_inv is invalid\n";
        for (int i3=0; i3<inEl[3]; i3++) {
        for (int i2=0; i2<inEl[2]; i2++) {
        for (int i1=0; i1<outEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent(); stream << name << "_1D.io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "] <= _inv.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "io.in.ready <= UInt<1>(1)\n";
        do_indent(); stream << "when io.in.valid :\n";
        do_indent(); stream << "  when geq(row, UInt<" << nBit_imgL1 << ">(" << bufL1 << ")) :\n";
        do_indent(); stream << "    node inSliceL1m = mul(rowModBufL1, UInt<" << nBit_inEl1+1 << ">(" << inEl[1] << "))\n";
        for(int l1=0; l1<bufL1; l1++) {
            do_indent(); stream << "    infer mport buffer" << l1 << "_rd = buffer" << l1 << "[col], clock\n";
            for (int i3=0; i3<inEl[3]; i3++) {
            for (int i2=0; i2<inEl[2]; i2++) {
            for (int i1=0; i1<inEl[1]; i1++) {
                if (inEl[1]==1) {
                    do_indent(); stream << "    node inSliceL1ma" << i3 << i2 << i1 << "_buffer" << l1 << " = inSliceL1m\n";
                } else {
                    do_indent(); stream << "    node inSliceL1ma" << i3 << i2 << i1 << "_buffer" << l1 << " = tail(add(inSliceL1m, UInt<" << nBit_outEl1 << ">(" << i1 << ")), 1)\n";
                }
                for (int i0=0; i0<inEl[0]; i0++) {
                    do_indent(); stream << "    slice.value[" << i3 << "][" << i2 << "][inSliceL1ma" << i3 << i2 << i1 << "_buffer" << l1 << "][" << i0 << "] <=  buffer" << l1 << "_rd.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
                }
            }
            }
            }
        }
        for (int i3=0; i3<inEl[3]; i3++) {
        for (int i2=0; i2<inEl[2]; i2++) {
        for (int i1=0; i1<inEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent(); stream << "    slice.value[" << i3 << "][" << i2 << "][" << bufL1*inEl[1]+i1 << "][" << i0 << "] <= io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }

        do_indent(); stream << "    " << name << "_1D.io.in.valid <= UInt<1>(1)\n";

        for (int i3=0; i3<outEl[3]; i3++) {
        for (int i2=0; i2<outEl[2]; i2++) {
        for (int i1=0; i1<outEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent(); stream << "    " << name << "_1D.io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "] <= slice.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "    skip\n";

        do_indent(); stream << "  when " << name << "_1D.io.in.ready :\n";
        do_indent(); stream << "    node col_is_max = eq(col, UInt<" << nBit_imgL0 << ">(" << imgL0-1 << "))\n";
        do_indent(); stream << "    node col_inc_c = add(col, UInt<1>(1))\n";
        do_indent(); stream << "    node col_inc = tail(col_inc_c, 1)\n";
        do_indent(); stream << "    col <= col_inc\n";
        do_indent(); stream << "    when col_is_max :\n";
        do_indent(); stream << "      col <= UInt<1>(0)\n";
        do_indent(); stream << "      skip\n";
        do_indent(); stream << "    when col_is_max :\n";
        do_indent(); stream << "      node row_is_max = eq(row, UInt<" << nBit_imgL1 << ">(" << imgL1-1 << "))\n";
        do_indent(); stream << "      node row_inc = tail(add(row, UInt<1>(1)), 1)\n";
        do_indent(); stream << "      row <= row_inc\n";
        do_indent(); stream << "      when row_is_max :\n";
        do_indent(); stream << "        row <= UInt<1>(0)\n";
        do_indent(); stream << "        skip\n";
        if (bufL1!=0) {
            do_indent(); stream << "      node rowModBufL1_is_max = eq(rowModBufL1, UInt<" << nBit_bufL1 << ">(" << bufL1-1 << "))\n";
            do_indent(); stream << "      node rowModBufL1_inc = tail(add(rowModBufL1, UInt<1>(1)), 1)\n";
            do_indent(); stream << "      rowModBufL1 <= rowModBufL1_inc\n";
        }
        do_indent(); stream << "      skip\n";

        for(int l1=0; l1<bufL1; l1++) {
            do_indent(); stream << "    when eq(UInt<" << nBit_bufL1 << ">(" << l1 << "), rowModBufL1) :\n";
            do_indent(); stream << "      infer mport buffer" << l1 << "_wr = buffer" << l1 << "[col], clock\n";
            for (int i3=0; i3<inEl[3]; i3++) {
            for (int i2=0; i2<inEl[2]; i2++) {
            for (int i1=0; i1<inEl[1]; i1++) {
            for (int i0=0; i0<inEl[0]; i0++) {
                do_indent(); stream << "      buffer" << l1 << "_wr.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "] <= io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
            }
            }
            }
            }
            do_indent(); stream << "      skip\n";
        }
        do_indent(); stream << "    skip\n";
        do_indent(); stream << "  skip\n";

        do_indent(); stream << "io.in.ready <= " << name << "_1D.io.in.ready\n";

        for (int i3=0; i3<outEl[3]; i3++) {
        for (int i2=0; i2<outEl[2]; i2++) {
        for (int i1=0; i1<outEl[1]; i1++) {
        for (int i0=0; i0<outEl[0]; i0++) {
            do_indent(); stream << "io.out.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "] <= " << name << "_1D.io.out.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }

        do_indent(); stream << "io.out.valid <= " << name << "_1D.io.out.valid\n";
        do_indent(); stream << name << "_1D.io.out.ready <= io.out.ready\n";
    }

    stream << "\n";
    close_scope("");

    if ((inEl[0]!=outEl[0]) || (inEl[1]!=outEl[1])) {
        inEl[1] = outEl[1];
        print_linebuffer1D(name, L, t, inEl, outEl);
    }
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_linebuffer3D(string name, int L[4], Type t, int inEl[4], int outEl[4])
{
    // TODO: require(....
    // TODO: require(inEl.dim(3) == outEl.dim(3) == 1)

    do_indent();
    stream << "module " << name << "_3D" << " :\n";
    open_scope();

    string inS = print_type(t) + "[" + std::to_string(inEl[0]) + "][" + std::to_string(inEl[1]) + "][" + std::to_string(inEl[2]) + "][" + std::to_string(inEl[3]) + "]";
    string outS = print_type(t) + "[" + std::to_string(outEl[0]) + "][" + std::to_string(outEl[1]) + "][" + std::to_string(outEl[2]) + "][" + std::to_string(outEl[3]) + "]";

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";
    do_indent();
    stream << "output io : {flip in : {flip ready : UInt<1>, valid : UInt<1>, bits : {value : " << inS << "}}, "
           << "out : {flip ready : UInt<1>, valid : UInt<1>, bits : {value : " << outS << "}}}\n";
    stream << "\n";

    do_indent(); stream << "clock is invalid\n";
    do_indent(); stream << "reset is invalid\n";
    do_indent(); stream << "io is invalid\n";

    if ((inEl[0] == outEl[0])&&(inEl[0] == L[0])) { // Trivial case, use 2D with stencil transformation.
        string l2S = print_type(t) + "[" + std::to_string(inEl[0]*inEl[1]) + "][" + std::to_string(inEl[2]) + "][" + std::to_string(inEl[3]) + "][1]";
        do_indent(); stream << "io.in.ready <= UInt<1>(0)\n";
        do_indent(); stream << "wire slice : {value : " << l2S << "}\n";
        do_indent(); stream << "slice is invalid\n";
        do_indent(); stream << "inst " << name << "_2D of " << name << "_2D" << "\n";
        do_indent(); stream << name << "_2D.io is invalid\n";
        do_indent(); stream << name << "_2D.clock <= clock\n";
        do_indent(); stream << name << "_2D.reset <= reset\n";
        do_indent(); stream << name << "_2D.io.in.valid <= UInt<1>(0)\n";
        do_indent(); stream << "wire _inv : {value : " << l2S << "}\n";
        do_indent(); stream << "_inv is invalid\n";
        for (int i3=0; i3<inEl[3]; i3++) {
        for (int i2=0; i2<inEl[2]; i2++) {
        for (int i1=0; i1<inEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent(); stream << name << "_2D.io.in.bits.value[0][" << i3 << "][" << i2 << "][" << i1*inEl[0]+i0 << "] ";
            stream << "<= _inv.value[0][" << i3 << "][" << i2 << "][" << i1*inEl[0]+i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "io.in.ready <= UInt<1>(1)\n";
        do_indent(); stream << "when io.in.valid :\n";
        do_indent(); stream << "  " << name << "_2D.io.in.valid <= UInt<1>(1)\n";
        for (int i3=0; i3<inEl[3]; i3++) { // 3D -> 2D
        for (int i2=0; i2<inEl[2]; i2++) {
        for (int i1=0; i1<inEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent(); stream << "  " << name << "_2D.io.in.bits.value[0][" << i3 << "][" << i2 << "][" << i1*inEl[0]+i0 << "]";
            stream << " <= io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "  skip\n";
        do_indent(); stream << "io.in.ready <= " << name << "_2D.io.in.ready\n";
        for (int i3=0; i3<outEl[3]; i3++) { // 2D -> 3D
        for (int i2=0; i2<outEl[2]; i2++) {
        for (int i1=0; i1<outEl[1]; i1++) {
        for (int i0=0; i0<outEl[0]; i0++) {
            do_indent(); stream << "io.out.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]";
            stream << " <= " << name << "_2D.io.out.bits.value[0][" << i3 << "][" << i2 << "][" << i1*outEl[0]+i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "io.out.valid <= " << name << "_2D.io.out.valid\n";
        do_indent(); stream << name << "_2D.io.out.ready <= io.out.ready\n";
        stream << "\n";
        close_scope("");


        inEl[0] = inEl[0]*inEl[1];
        inEl[1] = inEl[2];
        inEl[2] = inEl[3];
        outEl[0] = outEl[0]*outEl[1];
        outEl[1] = outEl[2];
        outEl[2] = outEl[3];
        L[0] = L[1];
        L[1] = L[2];
        L[2] = L[3];

        print_linebuffer2D(name, L, t, inEl, outEl);

    } else if ((inEl[2] == outEl[2])&&(inEl[2] == L[2])) { // Trivial case, use 2D with stencil transformation.
        string l2S = print_type(t) + "[" + std::to_string(inEl[0]) + "][" + std::to_string(inEl[1]*inEl[2]) + "][" + std::to_string(inEl[3]) + "][1]";
        do_indent(); stream << "io.in.ready <= UInt<1>(0)\n";
        do_indent(); stream << "wire slice : {value : " << l2S << "}\n";
        do_indent(); stream << "slice is invalid\n";
        do_indent(); stream << "inst " << name << "_2D of " << name << "_2D" << "\n";
        do_indent(); stream << name << "_2D.io is invalid\n";
        do_indent(); stream << name << "_2D.clock <= clock\n";
        do_indent(); stream << name << "_2D.reset <= reset\n";
        do_indent(); stream << name << "_2D.io.in.valid <= UInt<1>(0)\n";
        do_indent(); stream << "wire _inv : {value : " << l2S << "}\n";
        do_indent(); stream << "_inv is invalid\n";
        for (int i3=0; i3<inEl[3]; i3++) {
        for (int i2=0; i2<inEl[2]; i2++) {
        for (int i1=0; i1<inEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent(); stream << name << "_2D.io.in.bits.value[0][" << i3 << "][" << i2*inEl[1]+i1 << "][" << i0 << "] ";
            stream << "<= _inv.value[0][" << i3 << "][" << i2*inEl[1]+i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "io.in.ready <= UInt<1>(1)\n";
        do_indent(); stream << "when io.in.valid :\n";
        do_indent(); stream << "  " << name << "_2D.io.in.valid <= UInt<1>(1)\n";
        for (int i3=0; i3<inEl[3]; i3++) { // 3D -> 2D
        for (int i2=0; i2<inEl[2]; i2++) {
        for (int i1=0; i1<inEl[1]; i1++) {
        for (int i0=0; i0<inEl[0]; i0++) {
            do_indent(); stream << "  " << name << "_2D.io.in.bits.value[0][" << i3 << "][" << i2*inEl[1]+i1 << "][" << i0 << "]";
            stream << " <= io.in.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "  skip\n";
        do_indent(); stream << "io.in.ready <= " << name << "_2D.io.in.ready\n";
        for (int i3=0; i3<outEl[3]; i3++) { // 2D -> 3D
        for (int i2=0; i2<outEl[2]; i2++) {
        for (int i1=0; i1<outEl[1]; i1++) {
        for (int i0=0; i0<outEl[0]; i0++) {
            do_indent(); stream << "io.out.bits.value[" << i3 << "][" << i2 << "][" << i1 << "][" << i0 << "]";
            stream << " <= " << name << "_2D.io.out.bits.value[0][" << i3 << "][" << i2*outEl[1]+i1 << "][" << i0 << "]\n";
        }
        }
        }
        }
        do_indent(); stream << "io.out.valid <= " << name << "_2D.io.out.valid\n";
        do_indent(); stream << name << "_2D.io.out.ready <= io.out.ready\n";
        stream << "\n";
        close_scope("");


        inEl[0] = inEl[0];
        inEl[1] = inEl[1]*inEl[2];
        inEl[2] = inEl[3];
        outEl[0] = outEl[0];
        outEl[1] = outEl[1]*outEl[2];
        outEl[2] = outEl[3];
        L[0] = L[0];
        L[1] = L[1];
        L[2] = 1;

        print_linebuffer2D(name, L, t, inEl, outEl);

    } else {
        // TODO
        do_indent(); stream << "; Not supported yet\n";
    }

}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_dispatch(Dispatch *c)
{
    do_indent();
    stream << "module " << c->getModuleName() << " :\n";
    open_scope();

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";

    for(auto &p : c->getInPorts()) {
        do_indent();
        stream << "input " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << "output " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    // TODO: generate body of the Linebuffer.
    FIRRTL_Type in_stencil;
    string in_name;
    for(auto &p : c->getInputs()) { // only one input
        in_stencil = p.second;
        in_name = p.first;
        break;
    }
    in_stencil.type = FIRRTL_Type::StencilContainerType::Stencil; // stream -> stencil
    do_indent(); stream << "; Parameters:\n";
    do_indent(); stream << ";  Type=" << in_stencil.elemType << "\n";
    do_indent(); stream << ";  Bits=" << in_stencil.elemType.bits() << "\n";
    do_indent(); stream << ";  Stencil=";
    for(const auto &range : in_stencil.bounds) {
        stream << "[" << range.extent << "]";
    }
    stream << "\n";
    do_indent(); stream << ";  stencil_sizes=";
    for(const auto &s : c->getStencilSizes()) {
        stream << "[" << s << "]";
    }
    stream << "\n";
    do_indent(); stream << ";  stencil_steps=";
    for(const auto &s : c->getStencilSteps()) {
        stream << "[" << s << "]";
    }
    stream << "\n";
    do_indent(); stream << ";  store_extents=";
    for(const auto &s : c->getStoreExtents()) {
        stream << "[" << s << "]";
    }
    stream << "\n";
    do_indent(); stream << ";  fifo_depth   =";
    for(const auto &s : c->getConsumerFifoDepths()) {
        stream << "[" << s << "]";
    }
    stream << "\n";
    do_indent(); stream << ";  consumer_offset   =";
    for(const auto &s : c->getConsumerOffsets()) {
        stream << "[";
        for(const auto &b : s) {
            stream << " " << b;
        }
        stream << "]";
    }
    stream << "\n";
    do_indent(); stream << ";  consumer_extent   =";
    for(const auto &s : c->getConsumerExtents()) {
        stream << "[";
        for(const auto &b : s) {
            stream << " " << b;
        }
        stream << "]";
    }
    stream << "\n";
    do_indent(); stream << ";  consumer   =";
    vector<string> consumer_names;
    for(const auto &s : c->getOutputs()) {
        stream << s.first << " ";
        consumer_names.push_back(s.first);
    }
    stream << "\n";
    stream << "\n";

    // body
    vector<int> stencil_sizes = c->getStencilSizes();
    vector<int> stencil_steps = c->getStencilSteps();
    vector<int> store_extents = c->getStoreExtents();
    size_t num_of_dimensions = stencil_sizes.size();

    vector<int> consumer_fifo_depths = c->getConsumerFifoDepths();
    vector<vector<int> > consumer_offsets = c->getConsumerOffsets();
    vector<vector<int> > consumer_extents = c->getConsumerExtents();
    size_t num_of_consumers = consumer_fifo_depths.size();

    do_indent(); stream << "wire ST_IDLE : UInt<2>\n";
    do_indent(); stream << "wire ST_RUN0 : UInt<2>\n";
    do_indent(); stream << "wire ST_DONE : UInt<2>\n";
    stream << "\n";

    do_indent(); stream << "ST_IDLE <= UInt<2>(0)\n";
    do_indent(); stream << "ST_RUN0 <= UInt<2>(1)\n";
    do_indent(); stream << "ST_DONE <= UInt<2>(3)\n";
    stream << "\n";

    do_indent(); stream << "reg  r_cs_fsm : UInt<2>, clock with : (reset => (reset, UInt<16>(0)))\n";
    do_indent(); stream << "wire w_ns_fsm : UInt<2>\n";
    do_indent(); stream << "wire w_done : UInt<1>\n";
    stream << "\n";

    for(size_t i = 0; i < num_of_dimensions; i++) {
        do_indent(); stream << "reg  r_counter_var_" << i << " : UInt<16>, clock with : (reset => (reset, UInt<16>(0)))\n";
    }
    do_indent(); stream << "reg  r_data_out : " << print_stencil_type(in_stencil) << ", clock\n";
    do_indent(); stream << "reg  r_valid_out : UInt<1>, clock with : (reset => (reset, UInt<16>(0)))\n";

    do_indent(); stream << "wire ready_in_i : UInt<1>\n";
    do_indent(); stream << "wire valid_in_i : UInt<1>\n";
    do_indent(); stream << "wire data_in_i : " << print_stencil_type(in_stencil) << "\n";
    do_indent(); stream << "wire ready_out_i : UInt<1>\n";
    stream << "\n";

    do_indent(); stream << "ready_in_i <= ";
    for(size_t i = 0; i < num_of_consumers; i++) {
        if (i==(num_of_consumers-1)) // last
            stream << consumer_names[i] << ".ready";
        else
            stream << "or(" << consumer_names[i] << ".ready, ";
    }
    for(size_t i = 0; i < num_of_consumers-1; i++) {
        stream << ")"; // closing OR
    }
    stream << "\n";
    do_indent(); stream << "valid_in_i <= " << in_name << ".valid\n";
    do_indent(); stream << "data_in_i <= " << in_name << ".value\n";
    stream << "\n";

    for(size_t i = 0; i < num_of_dimensions; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_max : UInt<1>\n";
    }
    do_indent(); stream << "wire w_counter_all_max : UInt<1>\n";
    stream << "\n";

    for(size_t i = 0; i < num_of_dimensions; i++) {
        do_indent(); stream << "w_counter_var_" << i << "_max <= eq(r_counter_var_" << i << ", UInt<16>(" << (store_extents[i] - stencil_sizes[i]) << "))\n";
    }
    do_indent(); stream << "w_counter_all_max <= ";
    for(size_t i = 0; i < num_of_dimensions; i++) {
        if (i==(num_of_dimensions-1))
            stream << "w_counter_var_" << i << "_max";
        else
            stream << "and(w_counter_var_" << i << "_max, ";
    }
    for(size_t i = 0; i < num_of_dimensions-1; i++) {
        stream << ")"; // closing AND
    }
    stream << "\n";
    stream << "\n";

    for(size_t i = 0; i < num_of_dimensions; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_reset : UInt<1>\n";
    }
    for(size_t i = 0; i < num_of_dimensions; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_inc : UInt<1>\n";
    }
    for(size_t i = 0; i < num_of_consumers; i++) {
        for(size_t j = 0; j < num_of_dimensions; j++) {
            do_indent(); stream << "wire w_consumer" << i << "_var_" << j << "_ob : UInt<1>\n";
        }
        do_indent(); stream << "wire w_consumer" << i << "_ob : UInt<1>\n";
    }
    do_indent(); stream << "wire ready_valid : UInt<1>\n";
    stream << "\n";

    do_indent(); stream << "ready_valid <= and(or(not(r_valid_out), ready_in_i), and(valid_in_i, eq(r_cs_fsm, ST_RUN0)))\n";
    for(size_t i = 0; i < num_of_dimensions; i++) {
        do_indent(); stream << "w_counter_var_" << i << "_reset <= or(start_in, and(ready_valid, ";
        for(size_t j = 0 ; j <= i; j++) {
            if (j == i) { // last term
                stream << "w_counter_var_" << j << "_max";
            } else {
                stream << "and(w_counter_var_" << j << "_max, ";
            }
        }
        for(size_t j = 0 ; j <= i ; j++) { // closing AND
            stream << ")";
        }
        stream << ")\n"; // closing OR
    }
    stream << "\n";

    do_indent(); stream << "w_counter_var_0_inc <= ready_valid\n";
    if (num_of_dimensions>1) {
        do_indent(); stream << "w_counter_var_1_inc <= and(ready_valid, and(w_counter_var_0_max, not(w_counter_var_1_max)))\n";
    }
    if (num_of_dimensions>2) {
        do_indent(); stream << "w_counter_var_2_inc <= and(ready_valid, and(w_counter_var_0_max, and(w_counter_var_1_max, not(w_counter_var_2_max))))\n";
    }
    if (num_of_dimensions>3) {
        do_indent(); stream << "w_counter_var_3_inc <= and(ready_valid, and(w_counter_var_0_max, and(w_counter_var_1_max, and(w_counter_var_2_max, not(w_counter_var_3_max)))))\n";
    }
    stream << "\n";

    do_indent(); stream << "r_cs_fsm <= w_ns_fsm\n";
    stream << "\n";

    do_indent(); stream << "w_ns_fsm <= r_cs_fsm\n";
    do_indent(); stream << "when eq(r_cs_fsm, ST_IDLE) :\n";
    do_indent(); stream << "  when start_in :\n";
    do_indent(); stream << "    w_ns_fsm <= ST_RUN0\n";
    do_indent(); stream << "else when eq(r_cs_fsm, ST_RUN0) :\n";
    do_indent(); stream << "  when w_done :\n";
    do_indent(); stream << "    w_ns_fsm <= ST_DONE\n";
    do_indent(); stream << "else when eq(r_cs_fsm, ST_DONE) :\n";
    do_indent(); stream << "  w_ns_fsm <= ST_IDLE\n";
    stream << "\n";

    do_indent(); stream << "w_done <= and(w_counter_all_max, ready_valid)\n";
    stream << "\n";

    for(size_t i = 0; i < num_of_dimensions; i++) {
        do_indent(); stream << "when w_counter_var_" << i << "_reset :\n";
        do_indent(); stream << "  r_counter_var_" << i << " <= UInt<16>(0)\n";
        do_indent(); stream << "else when w_counter_var_" << i << "_inc :\n";
        do_indent(); stream << "  r_counter_var_" << i << " <= add(r_counter_var_" << i << ", UInt<16>(" << stencil_steps[i] << "))\n";
        stream << "\n";
    }

    do_indent(); stream << "ready_out_i <= or(not(r_valid_out), ready_in_i)\n";
    stream << "\n";

    do_indent(); stream << "when ready_valid :\n";
    do_indent(); stream << "  r_data_out <= data_in_i\n";
    stream << "\n";

    do_indent(); stream << "when ready_valid :\n";
    do_indent(); stream << "  r_valid_out <= UInt<1>(1)\n";
    do_indent(); stream << "else when ready_in_i :\n";
    do_indent(); stream << "  r_valid_out <= UInt<1>(0)\n";
    stream << "\n";

    for(size_t i = 0; i < num_of_consumers; i++) {
        for(size_t j = 0; j < num_of_dimensions; j++) {
            do_indent(); stream << "w_consumer" << i << "_var_" << j << "_ob <= or(lt(r_counter_var_" << j
                                << ", UInt<16>(" << consumer_offsets[i][j] << ")), gt(r_counter_var_" << j
                                << ", UInt<16>(" << (consumer_offsets[i][j]+consumer_extents[i][j]-stencil_sizes[j]) << ")))\n";
        }
        do_indent(); stream << "w_consumer" << i << "_ob <= ";
        for(size_t j = 0; j < num_of_dimensions; j++) {
            if (j==(num_of_dimensions-1))
                stream << "w_consumer" << i << "_var_" << j << "_ob";
            else
                stream << "or(w_consumer" << i << "_var_" << j << "_ob, ";
        }
        for(size_t j = 0; j < num_of_dimensions-1; j++) {
            stream << ")"; // closing OR
        }
        stream << "\n";
    }
    stream << "\n";

    do_indent(); stream << in_name << ".ready <= or(ready_out_i, ";
    for(size_t i = 0; i < num_of_consumers; i++) {
        if (i==(num_of_consumers-1))
            stream << "w_consumer" << i << "_ob";
        else
            stream << "and(w_consumer" << i << "_ob, ";
    }
    for(size_t i = 0; i < num_of_consumers-1; i++) {
        stream << ")";
    }
    stream << ")\n";
    stream << "\n";

    for(size_t i = 0; i < num_of_consumers; i++) {
        do_indent(); stream << consumer_names[i] << ".valid <= and(r_valid_out, not(w_consumer" << i << "_ob))\n";
        do_indent(); stream << consumer_names[i] << ".value <= r_data_out\n";
    }
    stream << "\n";

    do_indent(); stream << "done_out <= eq(r_cs_fsm, ST_DONE)\n";
    stream << "\n";

    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_forcontrol(ForControl *c)
{
    do_indent();
    stream << "module " << c->getModuleName() << " :\n";
    open_scope();

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";

    for(auto &p : c->getInPorts()) {
        do_indent();
        stream << "input " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << "output " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    do_indent(); stream << "; Parameters:\n";
    do_indent(); stream << ";  var=";
    for(auto &p : c->getVars()) {
        stream << p << " ";
    }
    stream << "\n";

    do_indent(); stream << ";  min=";
    for(auto &p : c->getMins()) {
        stream << p << " ";
    }
    stream << "\n";

    do_indent(); stream << ";  max=";
    for(auto &p : c->getMaxs()) {
        stream << p << " ";
    }
    stream << "\n";
    stream << "\n";

    // Body of ForControl
    vector<string> var = c->getVars();
    vector<string> stencil_var = c->getStencilVars();
    int num_var = var.size(); // outer loops
    int num_stencil_var = stencil_var.size(); // inner loops

    for(int i = 0 ; i < (num_stencil_var+num_var) ; i++) {
        do_indent(); stream << "wire VAR_" << i << "_MIN : UInt<16>\n";
        do_indent(); stream << "wire VAR_" << i << "_MAX : UInt<16>\n";
    }
    do_indent(); stream << "wire ST_IDLE  : UInt<2>\n";
    do_indent(); stream << "wire ST_RUN0  : UInt<2>\n";
    do_indent(); stream << "wire ST_RUN1  : UInt<2>\n";
    do_indent(); stream << "wire ST_FLUSH : UInt<2>\n";
    stream << "\n";

    vector<int> min = c->getMins();
    vector<int> max = c->getMaxs();

    for(int i = 0 ; i < (num_stencil_var+num_var) ; i++) {
        do_indent(); stream << "VAR_" << i << "_MIN <= UInt<16>(" << min[num_stencil_var+num_var - i - 1] << ")\n";
        do_indent(); stream << "VAR_" << i << "_MAX <= UInt<16>(" << max[num_stencil_var+num_var - i - 1] << ")\n";
    }
    stream << "\n";

    do_indent(); stream << "ST_IDLE   <= UInt<2>(0)\n";
    do_indent(); stream << "ST_RUN0   <= UInt<2>(1)\n";
    do_indent(); stream << "ST_RUN1   <= UInt<2>(2)\n";
    do_indent(); stream << "ST_FLUSH  <= UInt<2>(3)\n";
    stream << "\n";
    do_indent(); stream << "reg  r_cs_fsm : UInt<2>, clock with : (reset => (reset, UInt<2>(0)))\n";
    do_indent(); stream << "wire w_ns_fsm : UInt<2>\n";
    do_indent(); stream << "reg  r_done : UInt<1>, clock with : (reset => (reset, UInt<1>(0)))\n";
    do_indent(); stream << "wire w_run : UInt<1>\n";
    do_indent(); stream << "wire w_flush : UInt<1>\n";
    stream << "\n";

    for(int i = 0 ; i < num_var + num_stencil_var; i++) {
        do_indent(); stream << "reg  r_counter_var_" << i << " : UInt<16>, clock with : (reset => (reset, UInt<16>(0)))\n";
    }
    do_indent(); stream << "reg  r_flush_counter : UInt<8>, clock with : (reset => (reset, UInt<8>(0)))\n";
    stream << "\n";

    for(int i = 0 ; i < num_var + num_stencil_var; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_max : UInt<1>\n";
    }
    for(int i = 0 ; i < num_var + num_stencil_var; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_min : UInt<1>\n";
    }
    do_indent(); stream << "wire w_counter_all_max : UInt<1>\n";
    do_indent(); stream << "wire w_flush_done : UInt<1>\n";
    for(int i = 0 ; i < num_var + num_stencil_var; i++) {
        do_indent(); stream << "w_counter_var_" << i << "_max <= eq(r_counter_var_" << i << ", VAR_" << i << "_MAX)\n";
    }
    for(int i = 0 ; i < num_var + num_stencil_var; i++) {
        do_indent(); stream << "w_counter_var_" << i << "_min <= eq(r_counter_var_" << i << ", VAR_" << i << "_MIN)\n";
    }

    do_indent(); stream << "w_counter_all_max <= ";
    for(int i = num_stencil_var ; i < (num_stencil_var + num_var) ; i++) {
        if (i == (num_stencil_var+num_var-1)) // last term
            stream << "w_counter_var_" << i << "_max";
        else
            stream << "and(w_counter_var_" << i << "_max, ";
    }
    for(int i = 0 ; i < num_var-1 ; i++) {
        stream << ")";
    }
    stream << "\n";
    stream << "\n";

    for(int i = 0 ; i < num_var + num_stencil_var; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_reset : UInt<1>\n";
    }
    for(int i = 0 ; i < num_var + num_stencil_var; i++) {
        do_indent(); stream << "wire w_counter_var_" << i << "_inc : UInt<1>\n";
    }
    do_indent(); stream << "wire ready_valid : UInt<1>\n";
    stream << "\n";

    // input names are input stream names.
    map<string, FIRRTL_Type> streams = c->getInputs();
    vector<string> str;
    for(auto &i : streams) {
        str.push_back(i.first); // string name
    }
    internal_assert(str.size()!=0);
    do_indent(); stream << "ready_valid <= and(ready_in, and(eq(r_cs_fsm, ST_RUN0), ";
    for(unsigned i = 0 ; i < str.size() ; i++) {
        if (i == (str.size()-1)) stream << str[i];
        else stream << "and(" << str[i] << ", ";
    }
    for(unsigned i = 0 ; i < str.size()-1 ; i++) {
        stream << ")";
    }
    stream << "))\n";

    if (num_stencil_var>0) { // if there is stencil loop
        do_indent(); stream << "w_counter_var_0_reset <= or(start_in, w_counter_var_0_max)\n";
        if (num_stencil_var>1) {
            do_indent(); stream << "w_counter_var_1_reset <= or(start_in, and(w_counter_var_0_max, w_counter_var_1_max))\n";
        }
        if (num_stencil_var>2) {
            do_indent(); stream << "w_counter_var_2_reset <= or(start_in, and(w_counter_var_0_max, and(w_counter_var_1_max, w_counter_var_2_max)))\n";
        }
    }
    for(int i = num_stencil_var ; i < (num_stencil_var + num_var) ; i++) {
        do_indent(); stream << "w_counter_var_" << i << "_reset <= or(start_in, and(ready_valid, ";

        for(int j = num_stencil_var ; j <= i; j++) {
            if (j == i) { // last term
                stream << "w_counter_var_" << j << "_max";
            } else {
                stream << "and(w_counter_var_" << j << "_max, ";
            }
        }
        for(int j = num_stencil_var ; j <= i ; j++) { // closing AND
            stream << ")";
        }
        stream << ")\n"; // closing OR
    }
    stream << "\n";

    if (num_stencil_var>0) { // if there is stencil loop
        do_indent(); stream << "w_counter_var_0_inc <= or(ready_valid, eq(r_cs_fsm, ST_RUN1))\n";
        if (num_stencil_var>1) {
            do_indent(); stream << "w_counter_var_1_inc <= and(eq(r_cs_fsm, ST_RUN1), and(w_counter_var_0_max, not(w_counter_var_1_max)))\n";
        }
        if (num_stencil_var>2) {
            do_indent(); stream << "w_counter_var_2_inc <= and(eq(r_cs_fsm, ST_RUN1), and(w_counter_var_0_max, and(w_counter_var_1_max, not(w_counter_var_2_max))))\n";
        }
        // TODO: w_counter_var_3_inc Do we need?
    }
    for(int i = num_stencil_var ; i < (num_stencil_var + num_var) ; i++) {
        if (i==0) { // when there is no stencil loop
            do_indent(); stream << "w_counter_var_0_inc <= ready_valid\n";
        } else {
            do_indent(); stream << "w_counter_var_" << i << "_inc <= and(ready_valid, ";
            for(int j = 1 ; j <= i; j++) {
                if (j == i) { // last term
                    stream << "not(w_counter_var_" << j << "_max)";
                } else {
                    stream << "and(w_counter_var_" << j << "_max, ";
                }
            }
            for(int j = 1 ; j < i ; j++) { // closing AND
                stream << ")";
            }
            stream << ")\n"; // closing OR
        }
    }
    stream << "\n";

    // FSM
    do_indent(); stream << "r_cs_fsm <= w_ns_fsm\n";
    do_indent(); stream << "w_ns_fsm <= r_cs_fsm\n";
    do_indent(); stream << "when eq(r_cs_fsm, ST_IDLE) :\n";
    do_indent(); stream << "  when start_in :\n";
    do_indent(); stream << "    w_ns_fsm <= ST_RUN0\n";
    do_indent(); stream << "  else :\n";
    do_indent(); stream << "    skip\n";
    do_indent(); stream << "else when eq(r_cs_fsm, ST_RUN0) :\n";
    if (num_stencil_var>0) { // when there is stencil loop
        do_indent(); stream << "  when and(ready_in, ";
        for(unsigned i = 0 ; i < str.size() ; i++) {
            if (i == (str.size()-1)) stream << str[i];
            else stream << "and(" << str[i] << ", ";
        }
        for(unsigned i = 0 ; i < str.size()-1 ; i++) {
            stream << ")";
        }
        stream << ") :\n";
        do_indent(); stream << "    w_ns_fsm <= ST_RUN1\n";
        do_indent(); stream << "  else when r_done :\n";
        do_indent(); stream << "    w_ns_fsm <= ST_FLUSH\n";
        do_indent(); stream << "  else :\n";
        do_indent(); stream << "    skip\n";
        do_indent(); stream << "else when eq(r_cs_fsm, ST_RUN1) :\n";
        do_indent(); stream << "  when w_counter_var_0_max :\n";
        do_indent(); stream << "    w_ns_fsm <= ST_RUN0\n";
        do_indent(); stream << "  else :\n";
        do_indent(); stream << "    skip\n";
    } else {
        do_indent(); stream << "  when r_done :\n";
        do_indent(); stream << "    w_ns_fsm <= ST_FLUSH\n";
        do_indent(); stream << "  else :\n";
        do_indent(); stream << "    skip\n";
    }
    do_indent(); stream << "else when eq(r_cs_fsm, ST_FLUSH) :\n";
    do_indent(); stream << "  when w_flush_done :\n";
    do_indent(); stream << "    w_ns_fsm <= ST_IDLE\n";
    do_indent(); stream << "  else :\n";
    do_indent(); stream << "    skip\n";
    do_indent(); stream << "else :\n";
    do_indent(); stream << "  skip\n";
    stream << "\n";

    do_indent(); stream << "when start_in :\n";
    do_indent(); stream << "  r_done <= UInt<1>(0)\n";
    do_indent(); stream << "else when and(w_counter_all_max, ready_valid) :\n";
    do_indent(); stream << "  r_done <= UInt<1>(1)\n";
    do_indent(); stream << "else :\n";
    do_indent(); stream << "  skip\n";
    stream << "\n";

    do_indent(); stream << "when w_counter_var_0_reset :\n";
    do_indent(); stream << "  r_counter_var_0 <= VAR_0_MIN\n";
    do_indent(); stream << "else when w_counter_var_0_inc :\n";
    do_indent(); stream << "  r_counter_var_0 <= add(r_counter_var_0, UInt<1>(1))\n";
    do_indent(); stream << "else :\n";
    do_indent(); stream << "  skip\n";
    stream << "\n";

    if ((num_stencil_var+num_var)>1) {
        do_indent(); stream << "when w_counter_var_1_reset :\n";
        do_indent(); stream << "  r_counter_var_1 <= VAR_1_MIN\n";
        do_indent(); stream << "else when w_counter_var_1_inc :\n";
        do_indent(); stream << "  r_counter_var_1 <= add(r_counter_var_1, UInt<1>(1))\n";
        do_indent(); stream << "else :\n";
        do_indent(); stream << "  skip\n";
        stream << "\n";
    }

    if ((num_stencil_var+num_var)>2) {
        do_indent(); stream << "when w_counter_var_2_reset :\n";
        do_indent(); stream << "  r_counter_var_2 <= VAR_2_MIN\n";
        do_indent(); stream << "else when w_counter_var_2_inc :\n";
        do_indent(); stream << "  r_counter_var_2 <= add(r_counter_var_2, UInt<1>(1))\n";
        do_indent(); stream << "else :\n";
        do_indent(); stream << "  skip\n";
        stream << "\n";
    }
    // TODO: num_var>3

    do_indent(); stream << "when start_in :\n";
    do_indent(); stream << "  r_flush_counter <= UInt<8>(0)\n";
    do_indent(); stream << "else when w_run :\n";
    do_indent(); stream << "  when lt(r_flush_counter, depth) :\n";
    do_indent(); stream << "    r_flush_counter <= add(r_flush_counter, UInt<1>(1))\n";
    do_indent(); stream << "  else :\n";
    do_indent(); stream << "    skip\n";
    stream << "\n";

    if (num_stencil_var>0) {
        do_indent(); stream << "w_run <= or(ready_valid, eq(r_cs_fsm, ST_RUN1))\n";
    } else {
        do_indent(); stream << "w_run <= ready_valid\n";
    }
    do_indent(); stream << "w_flush <= and(eq(r_cs_fsm, ST_FLUSH), ready_in)\n";
    do_indent(); stream << "w_flush_done <= and(eq(r_cs_fsm, ST_FLUSH), eq(r_flush_counter, UInt<16>(0)))\n";
    do_indent(); stream << "enable <= or(w_run, w_flush)\n";
    do_indent(); stream << "load <= or(and(w_run, eq(r_flush_counter, depth)), w_flush)\n";
    if (num_stencil_var>0) {
        do_indent(); stream << "ready_out <= and(eq(r_cs_fsm, ST_RUN1), eq(w_ns_fsm, ST_RUN0))\n";
    } else {
        do_indent(); stream << "ready_out <= ready_valid\n";
    }
    stream << "\n";

    for(int i = 0 ; i < num_stencil_var ; i++) {
        do_indent(); stream << stencil_var[num_stencil_var - 1 - i] << " <= r_counter_var_" << i << "\n";
    }
    for(int i = 0 ; i < num_var ; i++) {
        do_indent(); stream << var[num_var - 1 - i] << " <= r_counter_var_" << i+num_stencil_var << "\n";
    }

    do_indent(); stream << "done_out <= r_done\n";
    stream << "\n";

    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_computestage(ComputeStage *c)
{
    do_indent();
    stream << "module " << c->getModuleName() << " :\n";
    open_scope();

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";

    for(auto &p : c->getInPorts()) {
        do_indent();
        stream << "input " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << "output " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    // Print Regs.
    do_indent();
    stream << "; Regs\n";
    for(auto &p : c->getRegs()) {
        do_indent();
        stream << "reg " << p.first << " : " << print_stencil_type(p.second) << ", clock\n";
    }
    stream << "\n";

    // Print Wires.
    do_indent();
    stream << "; Wires\n";
    for(auto &p : c->getWires()) {
        do_indent();
        stream << "wire " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    // Print connections.
    do_indent();
    stream << "; Connections\n";
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << p.first << " is invalid\n";
    }
    map<string, string> cn = c->getConnects();
    for(auto &p : c->getConnectKeys()) {
        do_indent();
        stream << p << " <= " << cn[p] << "\n";
    }
    stream << "\n";
    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_wrstream(WrStream *c)
{
    do_indent();
    stream << "module " << c->getModuleName() << " :\n";
    open_scope();

    // Print ports.
    do_indent();
    stream << "input clock : Clock\n";
    do_indent();
    stream << "input reset : UInt<1>\n";

    for(auto &p : c->getInPorts()) {
        do_indent();
        stream << "input " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    for(auto &p : c->getOutPorts()) {
        do_indent();
        stream << "output " << p.first << " : " << print_stencil_type(p.second) << "\n";
    }
    stream << "\n";

    vector<string> v = c->getStencilVars();
    vector<string> s = c->getVars();
    vector<int> m = c->getMaxs();

    do_indent(); stream << "; Parameters:\n";
    do_indent(); stream << ";  Stencil Vars=";
    for(unsigned i = 0; i < v.size(); i++ ) {
        stream << v[i] << " ";
    }
    stream << "\n";
    do_indent(); stream << ";  Maxs=";
    for(unsigned i = 0; i < m.size(); i++ ) {
        stream << m[i] << " ";
    }
    stream << "\n";
    stream << "\n";

    FIRRTL_Type outtype;
    for(auto &p : c->getOutputs()) { // TODO: assert expect only one
        outtype = p.second;
    }
    do_indent(); stream << "reg  r_valid_out : UInt<1>, clock with : (reset => (reset, UInt<1>(0)))\n";
    do_indent(); stream << "reg  r_data_out : " << print_stencil_type(outtype) << ", clock\n";
    do_indent(); stream << "wire valid_in : UInt<1>\n";
    stream << "\n";

    if (v.empty()) {
        do_indent(); stream << "valid_in <= load\n";
    } else {
        do_indent(); stream << "valid_in <= and(load, ";
        for(unsigned i = 0; i < v.size(); i++ ) {
            if (i == (v.size()-1))
                stream << "eq(" << v[i] << ", UInt<16>(" << m[i] << "))";
            else
                stream << "and(eq(" << v[i] << ", UInt<16>(" << m[i] << ")), ";
        }
        for(unsigned i = 0; i < v.size(); i++ ) {
            stream << ")\n";
        }
    }
    stream << "\n";

    do_indent(); stream << "when valid_in :\n";
    do_indent(); stream << "  r_valid_out <= UInt<1>(1)\n";
    do_indent(); stream << "else when ready_in :\n";
    do_indent(); stream << "  r_valid_out <= UInt<1>(0)\n";
    stream << "\n";

    do_indent(); stream << "when load :\n";
    if (v.empty()) {
        do_indent(); stream << "  r_data_out <= data_in\n";
    } else {
        // FIXME: more than one stencil variable m.size() > 1
        for(int j = 0; j <= m[0]; j++ ) {
            if (j==0) {
                do_indent(); stream << "  when eq(";
            } else  {
                do_indent(); stream << "  else when eq(";
            }
            for(unsigned i = 0; i < v.size(); i++ ) { // FIXME: v.size()==1 for now.
                stream << v[i] << ", UInt<16>(" << j << ")";
            }
            stream << ") :\n";
            do_indent(); stream << "    r_data_out";
            for(unsigned i = 0; i < s.size(); i++ ) {
                stream << "[0]";
            }
            for(unsigned i = 0; i < v.size(); i++ ) {
                stream << "[" << j << "]";
            }
            stream << " <= data_in";
            for(unsigned i = v.size(); i < v.size() + s.size(); i++ ) {
                stream << "[0]";
            }
            for(unsigned i = 0; i < v.size(); i++ ) {
                stream << "[" << j << "]";
            }
            stream << "\n";
        }
    }
    do_indent(); stream << "data_out <= r_data_out\n";
    do_indent(); stream << "valid_out <= r_valid_out\n";
    stream << "\n";

    close_scope("");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::open_scope()
{
    cache.clear();
    indent += 2;
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::close_scope(const std::string &comment)
{
    cache.clear();
    indent -= 2;
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Variable *op) {
    id = print_name(op->name);
}

// TODO: do we need?
//void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Cast *op) {
//}
void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Cast *op)
{
    print_assignment(op->type, "as" + print_base_type(op->type) + "(" + print_expr(op->value) + ")");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit_uniop(Type t, Expr a, const char * op) {
    string sa = print_expr(a);
    string sop(op);
    print_assignment(t, sop + "(" + sa + ")");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit_binop(Type t, Expr a, Expr b, const char * op) {
    string sa = print_expr(a);
    string sb = print_expr(b);
    string sop(op);
    debug(3) << "CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit_binop sa=" << sa << " sb=" << sb << " sop=" << op << "\n";
    print_assignment(t, sop + "(" + sa + ", " + sb + ")");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Add *op) {
    visit_binop(op->type, op->a, op->b, "add");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Sub *op) {
    //visit_binop(op->type, op->a, op->b, "sub");
    ostringstream oss; // TODO: better way?
    Type t;
    //oss << "asUInt(sub(" << print_expr(op->a) << ", " << print_expr(op->b) << "))";
    if ((op->type).is_uint()) {
        oss << "asUInt(sub(" << print_expr(op->a) << ", " << print_expr(op->b) << "))";
        t = UInt(op->type.bits());
    } else {
        oss << "sub(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        t = op->type;
    }
    print_assignment(t, oss.str());
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Mul *op) {
    visit_binop(op->type, op->a, op->b, "mul");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        ostringstream oss;
        oss << "dshr(" << print_expr(op->a) << ", UInt<32>(" << bits << "))"; // TODO is 32 proper?
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_div(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "div");
    }
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Mod *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        ostringstream oss;
        if ((op->type).is_uint()) {
            oss << "asUInt(";
        } else {
            oss << "asSInt(";
        }
        oss << "and(" << print_expr(op->a) << ", UInt<" << (op->type).bits() << ">(" << ((1 << bits)-1) << ")))";
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_mod(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "rem");
    }
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Max *op)
{
    //print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::Extern));
    // FIXME:
    Expr cond = op->a > op->b;
    Expr true_value = op->a;
    Expr false_value = op->b;
    Expr new_expr = Select::make(cond, true_value, false_value);
    //visit_binop(op->type, op->a, op->b, "max");
    print(new_expr);
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Min *op)
{
    //print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::Extern));
    // FIXME:
    Expr cond = op->a < op->b;
    Expr true_value = op->a;
    Expr false_value = op->b;
    Expr new_expr = Select::make(cond, true_value, false_value);
    //visit_binop(op->type, op->a, op->b, "min");
    print(new_expr);
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const EQ *op)
{
    visit_binop(op->type, op->a, op->b, "eq");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const NE *op)
{
    visit_binop(op->type, op->a, op->b, "neq");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const LT *op)
{
    visit_binop(op->type, op->a, op->b, "lt");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const LE *op)
{
    visit_binop(op->type, op->a, op->b, "leq");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const GT *op)
{
    visit_binop(op->type, op->a, op->b, "gt");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const GE *op)
{
    visit_binop(op->type, op->a, op->b, "geq");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const And *op)
{
    Type t = UInt((op->type).bits());
    visit_binop(t, op->a, op->b, "and");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Or *op)
{
    Type t = UInt((op->type).bits());
    visit_binop(t, op->a, op->b, "or");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Not *op)
{
    print_assignment(op->type, "not(" + print_expr(op->a) + ")");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const IntImm *op) {
    print_assignment(op->type, print_type(op->type) + "(" + std::to_string(op->value) + ")");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const UIntImm *op) {
    print_assignment(op->type, print_type(op->type) + "(" + std::to_string(op->value) + ")");
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const StringImm *op)
{
    ostringstream oss;
    oss << Expr(op);
    id = oss.str();
}

// NaN is the only float/double for which this is true... and
// surprisingly, there doesn't seem to be a portable isnan function
// (dsharlet).
template <typename T>
static bool isnan(T x) { return x != x; }

template <typename T>
static bool isinf(T x)
{
    return std::numeric_limits<T>::has_infinity && (
        x == std::numeric_limits<T>::infinity() ||
        x == -std::numeric_limits<T>::infinity());
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const FloatImm *op)
{
    // FIXME:
    internal_assert(true) << "Not support floating yet..\n";
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Call *op)
{
    FIRRTL_Type wire_1bit = {FIRRTL_Type::StencilContainerType::Scalar,UInt(1),Region(),0,{}};
    FIRRTL_Type wire_16bit = {FIRRTL_Type::StencilContainerType::Scalar,UInt(16),Region(),0,{}};

    if (op->is_intrinsic(Call::bitwise_and)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Type t = UInt((op->type).bits());
        visit_binop(t, a, b, "and");
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Type t = UInt((op->type).bits());
        visit_binop(t, a, b, "or");
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Type t = UInt((op->type).bits());
        visit_binop(t, a, b, "xor");
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        internal_assert(op->args.size() == 1);
        Expr a = op->args[0];
        visit_uniop(op->type, a, "not");
    } else if (op->is_intrinsic(Call::reinterpret)) {
        ostringstream rhs;
        internal_assert(op->args.size() == 1);
        Expr a = op->args[0];
        rhs << print_reinterpret(op->type, op->args[0]);
        print_assignment(op->type, rhs.str());
    } else if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        string sa = print_expr(a);
        string sb = print_expr(b);
        if (((op->type).bits())>16) { // FIRRTL has limitation this should be <20.
            print_assignment(op->type,  "dshl(" + sa + ", bits(" + sb + ", 18, 0))");
        } else {
            print_assignment(op->type,  "dshl(" + sa + ", " + sb + ")");
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Type t = UInt((op->type).bits());
        Expr cast_b = cast(t, b);
        visit_binop(op->type, a, cast_b, "dshr");
    } else if (op->is_intrinsic(Call::lerp)) {
        internal_error << "Call::lerp. What is this? Do we need to support?\n"; // TODO: Do we need this?
    } else if (op->is_intrinsic(Call::absd)) {
        ostringstream rhs;
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Expr e = select(a < b, b - a, a - b);
        rhs << print_expr(e);
        print_assignment(op->type, rhs.str());
    } else if (op->is_intrinsic(Call::abs)) {
        ostringstream rhs;
        internal_assert(op->args.size() == 1);
        Expr a0 = op->args[0];
        rhs << print_expr(cast(op->type, select(a0 > 0, a0, -a0)));
        Type t = UInt((op->type).bits());
        print_assignment(t, rhs.str());
    } else if (op->is_intrinsic(Call::div_round_to_zero)) {
        Expr a = op->args[0];
        Expr b = op->args[1];
        visit_binop(op->type, a, b, "div");
    } else if (op->is_intrinsic(Call::mod_round_to_zero)) {
        Expr a = op->args[0];
        Expr b = op->args[1];
        visit_binop(op->type, a, b, "rem");
    } else if(op->name == "linebuffer") {
        const Variable *input = op->args[0].as<Variable>();
        const Variable *output = op->args[1].as<Variable>();
        string inputname  = print_name(input->name);
        string outputname = print_name(output->name);
        FIRRTL_Type in_stype = top->getWire("wire_" + inputname); // get stencil type
        FIRRTL_Type out_stype = top->getWire("wire_" + outputname);

        // Create LineBuffer component
        LineBuffer *lb = new LineBuffer("LB_" + outputname);
        lb->addInput(inputname, in_stype);
        lb->addOutput(outputname, out_stype);
        size_t num_of_demensions = op->args.size() - 2;
        vector<int> store_extents(num_of_demensions);
        for (size_t i = 2; i < op->args.size(); i++) {
            const IntImm *int_imm = op->args[i].as<IntImm>();
            store_extents[i-2] = int_imm->value;
        }
        lb->setStoreExtents(store_extents);

        // Add to top
        top->addInstance(static_cast<Component*>(lb));

        // Connect clock/reset
        top->addConnect(lb->getInstanceName() + ".clock", "clock");
        top->addConnect(lb->getInstanceName() + ".reset", "reset");

        // Connect LineBuffer input port
        top->addConnect(lb->getInstanceName() + "." + inputname, "wire_" + inputname);     // LB.data_in <= wire

        // Connect LineBuffer Start/Done
        //string done = "LB_" + outputname + "_done";
        //sif->addInPort(done, wire_1bit);
        //lb->addInPort("start_in", wire_1bit);
        //lb->addOutPort("done_out", wire_1bit);
        //top->addConnect(lb->getInstanceName() + ".start_in", sif->getInstanceName() + ".start");  // LB.start_in <= SIF.start
        //top->addConnect(sif->getInstanceName() + "." + done, lb->getInstanceName() + ".done_out");// SIF.done <= LB.done_out

        // Create FIFO following LineBuffer
        FIFO *fifo = new FIFO("FIFO_" + outputname);
        fifo->addInput("data_in", out_stype);
        fifo->addOutput("data_out", out_stype);

        // Add to top
        top->addInstance(static_cast<Component*>(fifo));

        // Connect clock/reset
        top->addConnect(fifo->getInstanceName() + ".clock", "clock");
        top->addConnect(fifo->getInstanceName() + ".reset", "reset");

        // Connect FIFO input port
        top->addConnect(fifo->getInstanceName() + ".data_in", lb->getInstanceName() + "." + outputname);    // FIFO.data_in<=LB.data_out

        // Connect FIFO output port
        //TODO assert, (get->addWires()).contains("wire_" + outputname);
        top->addConnect("wire_" + outputname, fifo->getInstanceName() + ".data_out");  // wire <= FIFO.data_out

        id = "0";
    } else if (op->name == "write_stream") {
        // normal case
        // IR: write_stream(buffered.stencil_update.stream, buffered.stencil_update)
        const Variable *v0 = op->args[0].as<Variable>();
        const Variable *v1 = op->args[1].as<Variable>();
        string a0 = print_name(v0->name);
        string a1 = print_name(v1->name);
        FIRRTL_Type stream_type = top->getWire("wire_" + a0); // TODO: assert.
        FIRRTL_Type stencil_type = stream_type;
        stencil_type.type = FIRRTL_Type::StencilContainerType::Stencil; // data field only.
        // This is inside ComputeStage inside ForBlock.
        // Add output port at ComputeStage and ForBlock and connect them.
        // Add wire at top so that next block can find its stencil type.
        //   output stream_name : <type>
        //   reg r_stencil_name : <type>
        //   wire wire_stencil_name : <type>
        //   r_stencil_name <= wire_stencil_name
        //   stream_name <= r_stencil_name

        // WrStream was created at For just to add scan var port because our base design for WrStream requires that.
        //WrStream *ws = new WrStream("WS_" + a0);
        current_ws->addInput("data_in", stencil_type);
        current_ws->addInPort("load", wire_1bit);
        current_ws->addInPort("ready_in", wire_1bit);
        current_ws->addOutput("data_out", stencil_type);
        current_ws->addOutPort("valid_out", wire_1bit);

        // Connect clock/reset
        current_fb->addConnect(current_ws->getInstanceName() + ".clock", "clock");
        current_fb->addConnect(current_ws->getInstanceName() + ".reset", "reset");

        current_fb->addConnect(current_fc->getInstanceName() + ".ready_in", a0 + ".ready");
        current_fb->addConnect(current_ws->getInstanceName() + ".ready_in", a0 + ".ready");
        current_fb->addConnect(a0 + ".valid", current_ws->getInstanceName() + ".valid_out");
        current_fb->addConnect(current_ws->getInstanceName() + ".load", current_fb->getInstanceName() + ".load");

        current_cs->addOutput(a1, stencil_type);
        current_fb->addOutput(a0, stream_type);
        current_fb->addConnect(current_ws->getInstanceName() + ".data_in", current_cs->getInstanceName() + "." + a1);
        current_fb->addConnect(a0 + ".value", current_ws->getInstanceName() + ".data_out");
        current_fb->addConnect(current_ws->getInstanceName() + ".load", current_fc->getInstanceName() + ".load");

        for(int i= for_stencilvar_list.size()-1; i >= 0; i--) {
            current_ws->addInPort(for_stencilvar_list[i], wire_16bit);
            current_fb->addConnect(current_ws->getInstanceName() + "." + for_stencilvar_list[i], current_cs->getInstanceName() + "." + for_stencilvar_list[i] + "_out");
        }

        // Create FIFO following IO
        FIFO *fifo = new FIFO("FIFO_" + a0);
        fifo->addInput("data_in", stream_type);
        fifo->addOutput("data_out", stream_type);

        // Add to top
        top->addInstance(static_cast<Component*>(fifo));

        // Connect clock/reset
        top->addConnect(fifo->getInstanceName() + ".clock", "clock");
        top->addConnect(fifo->getInstanceName() + ".reset", "reset");

        // Connect FIFO input port from ForBlock output port
        top->addConnect(fifo->getInstanceName() + ".data_in", current_fb->getInstanceName() + "." + a0);

        // Connect FIFO output port
        top->addConnect("wire_" + a0, fifo->getInstanceName() + ".data_out");  // wire <= FIFO.data_out

        if (op->args.size() > 2) {
            // write stream call for the dag output kernel
            // IR: write_stream(output.stencil.stream, output.stencil, loop_var_1, loop_max_1, ...)

            // Create IO component for each input and output
            IO *interface = new IO("IO_" + a0, ComponentType::Output);

            // Add to top
            top->addInstance(static_cast<Component*>(interface));

            interface->addInput(a0, stream_type); // stream
            FIRRTL_Type stype = stream_type;
            stype.type = FIRRTL_Type::StencilContainerType::AxiStream;
            const Variable *v = op->args[0].as<Variable>();
            string arg_name = print_name(rootName(v->name)); // Use simple name for output.
            interface->addOutput(arg_name, stype); // axi stream
            interface->setStoreExtents(stream_type.store_extents);
            top->addOutput(arg_name, stype);
            //numOutputs++;

            // Connect clock/reset
            top->addConnect(interface->getInstanceName() + ".clock", "clock");
            top->addConnect(interface->getInstanceName() + ".reset", "reset");

            // Connect IO input port
            top->addConnect(interface->getInstanceName() + "." + a0, "wire_" + a0);   // IO.data_in <= wire_fifo_out

            // Connect IO output port
            top->addConnect(arg_name, interface->getInstanceName() + "." + arg_name);

            // Connect IO Start/Done
            string done = "IO_" + a0 + "_done";
            sif->addInPort(done, wire_1bit);
            interface->addInPort("start_in", wire_1bit);
            interface->addOutPort("done_out", wire_1bit);
            top->addConnect(interface->getInstanceName() + ".start_in", sif->getInstanceName() + ".start");    // IO.start_in <= SIF.start
            top->addConnect(sif->getInstanceName() + "." + done, interface->getInstanceName() + ".done_out");  // SIF.done <= IO.done_out
        }
    } else if (op->name == "read_stream") {
        internal_assert(op->args.size() == 2 || op->args.size() == 3);
        string a1 = print_name(print_expr(op->args[1]));

        const Variable *stream_name_var = op->args[0].as<Variable>();
        internal_assert(stream_name_var);
        string stream_name = print_name(stream_name_var->name);
        if (op->args.size() == 3) {
            // stream name is maggled with the consumer name
            const StringImm *consumer_imm = op->args[2].as<StringImm>();
            internal_assert(consumer_imm);
            stream_name += "_to_" + print_name(consumer_imm->value);
        }
        FIRRTL_Type stype = top->getWire("wire_" + stream_name); // get stencil type.
        current_fb->addInput(stream_name, stype);
        FIRRTL_Type stencil_type = stype;
        stencil_type.type = FIRRTL_Type::StencilContainerType::Stencil; // data field only will be input to ComputeState.
        current_cs->addInput(a1, stencil_type);
        current_fb->addConnect(current_cs->getInstanceName() + "." + a1, stream_name + ".value");
        top->addConnect(current_fb->getInstanceName() + "." + stream_name, "wire_" + stream_name);

        // connect valid/ready
        current_fc->addInput("valid_in_" + stream_name, wire_1bit);
        current_fb->addConnect(current_fc->getInstanceName() + ".valid_in_" + stream_name, stream_name + ".valid");
        current_fb->addConnect(stream_name + ".ready", current_fc->getInstanceName() + ".ready_out");
        id = "0";
    } else if (ends_with(op->name, ".stencil") ||
               ends_with(op->name, ".stencil_update")) {
        ostringstream rhs;
        // IR: out.stencil_update(0, 0, 0)
        // FIRRTL: out_stencil_update[0][0][0]
        //vector<string> args_indices(op->args.size());
        //vector<int> args_indices_int(op->args.size());
        rhs << print_name(op->name) << "[";
        //for(size_t i = 0; i < op->args.size(); i++) {
        //    const IntImm *a  = op->args[i].as<IntImm>();
        //    if (a) {
        //        args_indices[i] = "";
        //        args_indices_int[i] = a->value; // TODO: better way?
        //    } else {
        //        args_indices[i] = print_expr(op->args[i]);
        //    }
        //}
        for(int i = op->args.size()-1; i >= 0; i--) {
            const IntImm *a  = op->args[i].as<IntImm>();
            if (a) {
                rhs << std::to_string(a->value);
            } else {
                rhs << "asUInt(" + print_expr(op->args[i]) + ")";
            }
            if (i != 0)
                rhs << "][";
        }
        rhs << "]";

        print_assignment(op->type, rhs.str());
    } else if (op->name == "dispatch_stream") {
        // emits the calling arguments in comment
        vector<string> args(op->args.size());
        for(size_t i = 0; i < op->args.size(); i++)
            args[i] = print_name(print_expr(op->args[i]));

        // syntax:
        //   dispatch_stream(stream_name, num_of_dimensions,
        //                   stencil_size_dim_0, stencil_step_dim_0, store_extent_dim_0,
        //                   [stencil_size_dim_1, stencil_step_dim_1, store_extent_dim_1, ...]
        //                   num_of_consumers,
        //                   consumer_0_name, fifo_0_depth,
        //                   consumer_0_offset_dim_0, consumer_0_extent_dim_0,
        //                   [consumer_0_offset_dim_1, consumer_0_extent_dim_1, ...]
        //                   [consumer_1_name, ...])

        // recover the structed data from op->args
        internal_assert(op->args.size() >= 2);
        const Variable *stream_name_var = op->args[0].as<Variable>();
        internal_assert(stream_name_var);
        string stream_name = print_name(stream_name_var->name);
        size_t num_of_demensions = *as_const_int(op->args[1]);
        vector<int> stencil_sizes(num_of_demensions);
        vector<int> stencil_steps(num_of_demensions);
        vector<int> store_extents(num_of_demensions);

        internal_assert(op->args.size() >= num_of_demensions*3 + 2);
        for (size_t i = 0; i < num_of_demensions; i++) {
            stencil_sizes[i] = *as_const_int(op->args[i*3 + 2]);
            stencil_steps[i] = *as_const_int(op->args[i*3 + 3]);
            store_extents[i] = *as_const_int(op->args[i*3 + 4]);
        }

        internal_assert(op->args.size() >= num_of_demensions*3 + 3);
        size_t num_of_consumers = *as_const_int(op->args[num_of_demensions*3 + 2]);
        vector<string> consumer_names(num_of_consumers);
        vector<int> consumer_fifo_depth(num_of_consumers);
        vector<vector<int> > consumer_offsets(num_of_consumers);
        vector<vector<int> > consumer_extents(num_of_consumers);

        internal_assert(op->args.size() >= num_of_demensions*3 + 3 + num_of_consumers*(2 + 2*num_of_demensions));
        for (size_t i = 0; i < num_of_consumers; i++) {
            const StringImm *string_imm = op->args[num_of_demensions*3 + 3 + (2 + 2*num_of_demensions)*i].as<StringImm>();
            internal_assert(string_imm);
            consumer_names[i] = string_imm->value;
            const IntImm *int_imm = op->args[num_of_demensions*3 + 4 + (2 + 2*num_of_demensions)*i].as<IntImm>();
            internal_assert(int_imm);
            consumer_fifo_depth[i] = int_imm->value; // TODO: We will calculate this automatically.
            vector<int> offsets(num_of_demensions);
            vector<int > extents(num_of_demensions);
            for (size_t j = 0; j < num_of_demensions; j++) {
                offsets[j] = *as_const_int(op->args[num_of_demensions*3 + 5 + (2 + 2*num_of_demensions)*i + 2*j]);
                extents[j] = *as_const_int(op->args[num_of_demensions*3 + 6 + (2 + 2*num_of_demensions)*i + 2*j]);
            }
            consumer_offsets[i] = offsets;
            consumer_extents[i] = extents;
        }

        // emits declarations of streams for each consumer
        //internal_assert(stencils.contains(stream_name));
        //Stencil_Type stream_type = stencils.get(stream_name);
        FIRRTL_Type stream_type = top->getWire("wire_" + stream_name);

        // Optimization. if there is only one consumer and its fifo depth is zero
        // , use wire connection for the consumer stream
        if (num_of_consumers == 1 && consumer_fifo_depth[0] == 0) {
            string consumer_stream_name = stream_name + "_to_" + print_name(consumer_names[0]);
            //stream << print_stencil_type(stream_type) << " &"
            //       << print_name(consumer_stream_name) << " = "
            //       << print_name(stream_name) << ";\n";
            top->addWire("wire_" + consumer_stream_name, stream_type);
            top->addConnect("wire_" + consumer_stream_name, "wire_" + stream_name);

            id = "0"; // skip evaluation
            return;
        }

        for (size_t i = 0; i < num_of_consumers; i++) {
            consumer_fifo_depth[i] = std::max(consumer_fifo_depth[i], 1);// set minimum. TODO: We will calculate this automatically.
        }
        // Create Dispatch component
        Dispatch *dp = new Dispatch("DP_" + stream_name);
        dp->addInput(stream_name, stream_type);
        dp->setStencilSizes(stencil_sizes); // TODO Can't it be gotten from stencil_type?
        dp->setStencilSteps(stencil_steps);
        dp->setStoreExtents(store_extents);
        dp->setConsumerFifoDepths(consumer_fifo_depth);
        dp->setConsumerOffsets(consumer_offsets);
        dp->setConsumerExtents(consumer_extents);

        // Add to top
        top->addInstance(static_cast<Component*>(dp));

        // Connect clock/reset
        top->addConnect(dp->getInstanceName() + ".clock", "clock");
        top->addConnect(dp->getInstanceName() + ".reset", "reset");

        // Connect Dispatch input port
        top->addConnect(dp->getInstanceName() + "." + stream_name, "wire_" + stream_name);

        // Connect Dispatch Start/Done
        string done = "DP_" + stream_name + "_done";
        sif->addInPort(done, wire_1bit);
        dp->addInPort("start_in", wire_1bit);
        dp->addOutPort("done_out", wire_1bit);
        top->addConnect(dp->getInstanceName() + ".start_in", sif->getInstanceName() + ".start");   // DP.start_in <= SIF.start
        top->addConnect(sif->getInstanceName() + "." + done, dp->getInstanceName() + ".done_out"); // SIF.done <= DP.done_out

        for (size_t i = 0; i < num_of_consumers; i++) {
            string consumer_stream_name = stream_name + "_to_" + print_name(consumer_names[i]);
            dp->addOutput(consumer_stream_name, stream_type);

            // Create FIFO following Dispatch for each output.
            FIFO *fifo = new FIFO("FIFO_" + consumer_stream_name);
            fifo->addInput("data_in", stream_type);
            fifo->addOutput("data_out", stream_type);
            //fifo->setDepth("$$"); // Mark to be back-annotated later TODO
            fifo->setDepth(std::to_string(consumer_fifo_depth[i])); // FIXME later

            // Add to top
            top->addInstance(static_cast<Component*>(fifo));

            // Connect clock/reset
            top->addConnect(fifo->getInstanceName() + ".clock", "clock");
            top->addConnect(fifo->getInstanceName() + ".reset", "reset");

            // Connect FIFO input port
            top->addConnect(fifo->getInstanceName() + ".data_in",  dp->getInstanceName() + "." + consumer_stream_name);

            // Connect FIFO output port
            top->addWire("wire_" + consumer_stream_name, stream_type);
            top->addConnect("wire_" + consumer_stream_name, fifo->getInstanceName() + ".data_out");
        }

        id = "0";
    }
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Load *op)
{
    internal_error << "Load is not supported.\n"; // TODO
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Store *op)
{
    internal_error << "Store is not supported.\n"; // TODO

    cache.clear();
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Let *op)
{
    string id_value = print_expr(op->value);
    Expr new_var = Variable::make(op->value.type(), id_value);
    Expr body = substitute(op->name, new_var, op->body);
    print_expr(body);
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Select *op)
{
    ostringstream rhs;
    string type;
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);

    if ((op->type).is_uint()) {
        type = "asUInt(";
    } else {
        type = "asSInt(";
    }
    rhs << type << "mux(" << cond
        << ", " << type << true_val << ")"
        << ", " << type << false_val << ")))";
    print_assignment(op->type, rhs.str());
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const LetStmt *op)
{
    stream << "; LetStmt ??\n"; // FIXME
    string id_value = print_expr(op->value);
    Expr new_var = Variable::make(op->value.type(), id_value);
    Stmt body = substitute(op->name, new_var, op->body);
    body.accept(this);
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const AssertStmt *op)
{
    internal_error << "AsserStmt is not supported.\n";
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const ProducerConsumer *op)
{
    if (ends_with(op->name, ".stream")) {
        producename = op->name; // keep last ProcuderConsumer name as a produce name to be used in ForBlock naming.
    }
    print_stmt(op->body);
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const For *op)
{
    internal_assert(op->for_type == ForType::Serial)
        << "Can only emit serial for loops to FIRRTL\n";

    FIRRTL_Type wire_1bit = {FIRRTL_Type::StencilContainerType::Scalar,UInt(1),Region(),0,{}};
    FIRRTL_Type wire_8bit = {FIRRTL_Type::StencilContainerType::Scalar,UInt(8),Region(),0,{}};
    FIRRTL_Type wire_16bit = {FIRRTL_Type::StencilContainerType::Scalar,UInt(16),Region(),0,{}};

    int id_min = ((op->min).as<IntImm>())->value;
    int id_extent = ((op->extent).as<IntImm>())->value;

    if (for_scanvar_list.empty()) { // only one ForControl, ForBlock, ComputeStage per For statement group.

        // TODO assert current_fb, current_cs is null.

        // Create ForControl component
        string var_name = print_name(op->name);
        ForControl *fc = new ForControl("FC_" + print_name(producename));
        fc->addInPort("start_in", wire_1bit);
        fc->addOutPort("done_out", wire_1bit);
        fc->addInPort("ready_in", wire_1bit); // from back stage
        fc->addOutPort("ready_out", wire_1bit); // to previous stage
        fc->addOutPort("enable", wire_1bit); // to ComputeStage
        fc->addOutPort(var_name, wire_16bit); // to ComputeStage
        fc->addOutPort("load", wire_1bit); // to WrStream
        fc->addInPort("depth", wire_8bit); // parameter constant
        fc->addVar(var_name);
        fc->addMin(id_min);
        fc->addMax(id_extent-1);
        for_scanvar_list.push_back(var_name); // for ForBlock generation
        current_fc = fc; // for ForBlock generation

        // Create ForBlock component
        ForBlock *fb = new ForBlock("FB_" + print_name(producename));
        current_fb = fb;

        // Add to top
        top->addInstance(static_cast<Component*>(fb));

        // Create ComputeStage component
        ComputeStage *cs = new ComputeStage("CS_" + print_name(producename));
        current_cs = cs;
        cs->addInPort("enable", wire_1bit); // from ForControl

        // Add to ForBlock
        fb->addInstance(static_cast<Component*>(cs));
        fb->addInstance(static_cast<Component*>(fc));

        // Connect clock/reset
        fb->addConnect(fc->getInstanceName() + ".clock", "clock");
        fb->addConnect(fc->getInstanceName() + ".reset", "reset");
        fb->addConnect(cs->getInstanceName() + ".clock", "clock");
        fb->addConnect(cs->getInstanceName() + ".reset", "reset");
        top->addConnect(fb->getInstanceName() + ".clock", "clock");
        top->addConnect(fb->getInstanceName() + ".reset", "reset");

        // Connect Start/Done
        string done = "FB_" + print_name(producename) + "_done";
        sif->addInPort(done, wire_1bit);
        fb->addInPort("start_in", wire_1bit);
        fb->addOutPort("done_out", wire_1bit);
        top->addConnect(fb->getInstanceName() + ".start_in", sif->getInstanceName() + ".start");
        top->addConnect(sif->getInstanceName() + "." + done, fb->getInstanceName() + ".done_out");
        fb->addConnect(fc->getInstanceName() + ".start_in", "start_in");
        fb->addConnect("done_out", fc->getInstanceName() + ".done_out");

        // Constant parameter
        //fb->addConnect(fc->getInstanceName() + "." + "depth", "UInt<8>($$)"); // TODO is 8-bit enough? TODO, back-annotation after re-timing.
        fb->addConnect(fc->getInstanceName() + "." + "depth", "UInt<8>(0)"); // FIXME later

        // Connect loop variable between ForControl and ComputeStage
        cs->addInPort(var_name, wire_16bit);
        fb->addConnect(cs->getInstanceName() + "." + var_name, fc->getInstanceName() + "." + var_name);

        // Controls
        fb->addConnect(cs->getInstanceName() + ".enable", fc->getInstanceName() + ".enable");

        // Create WrStream here just to add scan var port because our base design for WrStream requires that.
        WrStream *ws = new WrStream("WS_" + print_name(producename));
        ws->addVar(var_name);
        current_ws = ws;
        fb->addInstance(static_cast<Component*>(ws));

        // Add parameter ports and connect them
        FIRRTL_For_Closure c(op->body);
        // Note: Outermost op->name can be added to Closure because only op->body is processed.
        // op->name will be excluded from Closure result by checking for_scanvar_list[0].
        vector<string> args = c.arguments(); // extract used variables.
        for(auto &s: args) { // Create ports and connect for variables.
            string a = print_name(s);
            if (for_scanvar_list[0] != a) { // ignore scan var
                FIRRTL_Type stype = top->getWire("wire_" + a);
                cs->addInPort(a, stype);
                fb->addInPort(a, stype);
                fb->addConnect(cs->getInstanceName() + "." + a, a);
                top->addConnect(fb->getInstanceName() + "." + a, "wire_" + a);
            }
        }

    } else {
        // If ForControl is already created, just add loop variable ports and parameters.
        string var_name = print_name(op->name);
        current_fc->addOutPort(var_name, wire_16bit);
        current_fc->addMin(id_min);
        current_fc->addMax(id_extent-1);
        if (!contain_read_stream(op->body)) { // iteration over stencil TODO: better way?
            current_fc->addStencilVar(var_name);
            for_stencilvar_list.push_back(var_name);

            // Connect loop variable between ComputeStage and WrStream
            current_cs->addOutPort(var_name+"_out", wire_16bit);
            current_ws->addInPort(var_name, wire_16bit);
            current_ws->addStencilVar(var_name);
            current_ws->addMax(id_extent-1); // to know the end of stencil update
            current_fb->addConnect(current_ws->getInstanceName() + "." + var_name, current_cs->getInstanceName() + "." + var_name + "_out");
        } else {
            current_fc->addVar(var_name);
            current_ws->addVar(var_name);
            for_scanvar_list.push_back(var_name);
        }

        // Connect loop variable between ForControl and ComputeStage
        current_cs->addInPort(var_name, wire_16bit);
        current_fb->addConnect(current_cs->getInstanceName() + "." + var_name, current_fc->getInstanceName() + "." + var_name);

    }

    open_scope();

    if (!contain_for_loop(op->body)) { // inner most loop
        // TODO: Do we need to keep this?
    }

    print(op->body);

    close_scope("");

    if (!contain_read_stream(op->body)) { // iteration over stencil
        for_stencilvar_list.pop_back();
    } else {
        for_scanvar_list.pop_back();
    }
    if (for_scanvar_list.empty()) {
        current_fc = nullptr;
        current_fb = nullptr;
        current_cs = nullptr;
        current_ws = nullptr;
    }
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Provide *op)
{
    if (ends_with(op->name, ".stencil") ||
        ends_with(op->name, ".stencil_update")) {
        ostringstream oss;
        // IR: buffered.stencil_update(1, 2, 3) =
        // FIRRTL: buffered_stencil_update[1][2][3] =
        //vector<string> args_indices(op->args.size());
        vector<string> args_indices(op->args.size());
        for(size_t i = 0; i < op->args.size(); i++)
            args_indices[i] = "asUInt(" + print_expr(op->args[i]) + ")";

        internal_assert(op->values.size() == 1);
        string id_value = print_expr(op->values[0]);

        oss << print_name(op->name) << "[";

        for(int i = op->args.size()-1; i >= 0; i--) { // reverse order in FIRRTL
            oss << args_indices[i];
            if (i != 0)
                oss << "][";
        }
        oss << "]";
        if (current_cs!=nullptr) {
            current_cs->addConnect(oss.str(), id_value);
        } else {
            top->addConnect(oss.str(), id_value);
        }

        cache.clear();
    } else {
        IRPrinter::visit(op);
    }
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Allocate *op)
{
    stream << "reg " << op->name << ": ____TODO____\n";
    print(op->body);
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Free *op)
{
    internal_error << "Free is not supported.\n";
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Realize *op)
{
    if (ends_with(op->name, ".stream")) {
        internal_assert(op->types.size() == 1);
        //allocations.push(op->name, {op->types[0], "null"}); // TODO: do we need this?
        std::vector<int> store_extents;
        for(size_t i = 0; i < op->bounds.size(); i++) store_extents.push_back(1); // default
        FIRRTL_Type stream_type({FIRRTL_Type::StencilContainerType::Stream,
                    op->types[0], op->bounds, 1, store_extents});
        top->addWire("wire_" + print_name(op->name), stream_type);

        // traverse down
        op->body.accept(this);

        //allocations.pop(op->name);

    } else if (ends_with(op->name, ".stencil") ||
               ends_with(op->name, ".stencil_update")) {
        //internal_assert(op->types.size() == 1);
        ////allocations.push(op->name, {op->types[0], "null"}); // TODO: do we need this?
        //std::vector<int> store_extents;
        //for(size_t i = 0; i < op->bounds.size(); i++) store_extents.push_back(1); // default
        //FIRRTL_Type stream_type({FIRRTL_Type::StencilContainerType::Stencil,
        //            op->types[0], op->bounds, 1, store_extents});
        //current_cs->addReg("r_" + print_name(op->name), stream_type);

        op->body.accept(this);

        //allocations.pop(op->name);
    } else {
        visit(op);
    }
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const IfThenElse *op) {
    if (contain_read_stream(op->then_case)||
        contain_write_stream(op->then_case)) {
        // Ignore. It's function is done by ForControl.
        IRVisitor::visit(op);
    } else {
        internal_error << "IfThenElse is not supported yet.\n"; // TODO
    }
    id = "0";
}

void CodeGen_FIRRTL_Target::CodeGen_FIRRTL::visit(const Evaluate *op)
{
    if (is_const(op->value)) return;
    string id = print_expr(op->value);
}

}
}
