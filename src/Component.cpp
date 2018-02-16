#include <stdio.h>
#include <iostream>
#include <vector>
#include <regex>
#include "Component.h"

namespace Halide {

namespace Internal {

using namespace std;

std::map<string, Component* > Component::components;

// Similar to CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_type() but for module name.
string Component::print_type(const Type type) {
    ostringstream oss;

    if (type.is_uint()) oss << "U";
    else oss << "S";

    oss << "Int" << type.bits();

    return oss.str();
}

// Similar to CodeGen_FIRRTL_Target::CodeGen_FIRRTL::print_stencil_type(FIRRTL_Type stencil_type) but for module name.
string Component::print_stencil_type(FIRRTL_Type stencil_type) {
    ostringstream oss;

    switch(stencil_type.type) {
    case FIRRTL_Type::StencilContainerType::Scalar :
        oss << print_type(stencil_type.elemType);
        break;
    case FIRRTL_Type::StencilContainerType::Stencil :
    case FIRRTL_Type::StencilContainerType::Stream :
        oss << print_type(stencil_type.elemType);
        for(size_t i = 0 ; i < stencil_type.bounds.size() ; i++ ) {
            if (i==0) {
                oss << "_";
            } else {
                oss << "x";
            }
            oss << stencil_type.bounds[i].extent;
        }
        break;
    case FIRRTL_Type::StencilContainerType::AxiStream :
        internal_error;
        break;
    default: internal_error;
    }
    return oss.str();
}
string Component::createModuleName(void)
{
    string name = "";
    if (this->type == ComponentType::Fifo) {
        // Create module name based on the parameters 
        // so that FIFO can be re-used in multiple instances.
        FIFO * c = static_cast<FIFO*>(this);
        FIRRTL_Type s;
        for(auto &i : c->getInputs()) {
            s = i.second;
            break;  // only one input and output each with same stencil type.
        }
        name += "FIFO_" + print_stencil_type(s) + "_" + c->getDepth();
    //} else if (this->type == ComponentType::Input) {
    //    for(auto &i : c->getInputs()) {
    //        name += "IO_" + i.first;
    //        break;  // only one input
    //    }
    //} else if (this->type == ComponentType::Output) {
    //    for(auto &i : c->getOutput()) {
    //        name += "IO_" + i.first;
    //        break;  // only one output
    //    }
    //} else if (this->type == ComponentType::Wrstream) {
    //    // Create module name based on the parameters 
    //    // so that FIFO can be re-used in multiple instances.
    //    WrStream * c = static_cast<WrStream*>(this);
    //    FIRRTL_Type s;
    //    for(auto &i : c->getInputs()) {
    //        s = i.second;
    //        break;  // only one input and output each with same stencil type.
    //    }
    //    name += "WS_" + print_stencil_type(s);
    } else {
        name += (this)->getInstanceName(); // by default instance name is module name.
    }

    return name;
}

string Component::getModuleName(void)
{
    return (moduleName!="")? moduleName : this->createModuleName();
}

void Component::addInstance(Component * c)
{
    string modulename = c->createModuleName();

    if (!components[modulename]) {
        components[modulename] = c;
    } else {
        // Same component already has generated. Should be only for FIFO. TODO. How about linebuffer?
    }

    instances[c->getInstanceName()] = modulename;
}

// Returns the vector<Component*> of omponentType == type.
vector<Component*> Component::getComponents(ComponentType type)
{
    vector<Component*> c;
    for(auto &i : components) {
        if (((i.second)->getType() == type)||(type == ComponentType::All)) c.push_back(i.second);
    }
    return c;
}

void Component::addWire(string p, FIRRTL_Type s)
{
    debug(3) << "addWire[" << p << "] " << print_stencil_type(s) << "\n";
    if (!wires.count(p)) {
        wires[p] = s;
    } else {
        debug(3) << "not added\n";
    }
}

void Component::addReg(string p, FIRRTL_Type s)
{
    debug(3) << "addReg[" << p << "] " << print_stencil_type(s) << "\n";
    if (!regs.count(p)) {
        regs[p] = s;
    } else {
        debug(3) << "not added\n";
        // TODO assert
    }
}

void ForBlock::open_scope()
{
    indent += 2;
}

void ForBlock::close_scope(const std::string &comment)
{
    indent -= 2;
}

void ForBlock::print(string s) {
    for (int i = 0; i < indent; i++) oss_body << ' ';
    oss_body << s;
}

vector<string> ForBlock::print_body() {
    string str = oss_body.str();
    vector<string> res;

    while(str.size()) {
        size_t found = str.find("\n");
        if (found!=string::npos) {
            res.push_back(str.substr(0,found));
            str = str.substr(found+1);
        } else {
            res.push_back(str);
            str = "";
        }
    }
    return res;
}

}
}
