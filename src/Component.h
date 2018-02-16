#ifndef HALIDE_HLS_COMPONENTS_H
#define HALIDE_HLS_COMPONENTS_H

#include <stdio.h>
#include <iostream>
#include <vector>
#include <regex>
#include "CodeGen_FIRRTL_Base.h"
//#include "CodeGen_FIRRTL_Target.h"

namespace Halide {

namespace Internal {

using namespace std;
enum ComponentType
{
    Input,
    Output,
    TopLvl,
    //Memory,
    Linebuffer,
    Fifo,
    Dispatcher,
    Forblock,
    Slaveif,
    All
};

class Component {
public:
    Component(const string &name) : instanceName(name) {}
    Component(const string &name, ComponentType type) : instanceName(name), type(type) {}

    Component() {}

    // Component Type
    void setType(ComponentType t) {type = t;}
    ComponentType getType() {return type;}

    // Component
    Component* getComponent(string name) {return components[name];}
    vector<Component*> getComponents(ComponentType type);

    // Instance Name
    void setInstanceName(string s) {instanceName = s;}
    string getInstanceName(void) {return instanceName;}

    // Module Name
    string getModuleName(void);
    string createModuleName(void);

    // Input/Output/Wire/Connection
    void addInPort(string p, FIRRTL_Type s) {input_ports[p] = s;}
    void addOutPort(string p, FIRRTL_Type s) {output_ports[p] = s;}
    void addInput(string p, FIRRTL_Type s) {input_streams[p] = s;addInPort(p,s);}
    void addOutput(string p, FIRRTL_Type s) {output_streams[p] = s;addOutPort(p,s);}
    void addWire(string p, FIRRTL_Type s);
    void addReg(string p, FIRRTL_Type s);
    void addConnect(string lhs, string rhs) {connections[lhs] = rhs;connection_keys.push_back(lhs);}

    map<string, FIRRTL_Type> getInPorts(void) {return input_ports;}
    map<string, FIRRTL_Type> getOutPorts(void) {return output_ports;}
    map<string, FIRRTL_Type> getInputs(void) {return input_streams;}
    map<string, FIRRTL_Type> getOutputs(void) {return output_streams;}
    FIRRTL_Type getWire(string p) {return wires[p];}
    map<string, FIRRTL_Type> getWires(void) {return wires;}
    map<string, FIRRTL_Type> getRegs(void) {return regs;}
    FIRRTL_Type getReg(string regname) {return regs[regname];}
    map<string, string> getConnects(void) {return connections;}
    vector<string> getConnectKeys(void) {return connection_keys;}

    // Instance
    void addInstance(Component * c);
    map<string, string> getInstances(void) {return instances;}

    string print_type(Type);
    string print_stencil_type(FIRRTL_Type);

protected:
    //string streamName; // stream name
    string instanceName; // instance name
    ComponentType type; // TODO do we need this? we can use as<>?
    string moduleName; // module name
    map<string, FIRRTL_Type> input_ports;      // Input port name
    map<string, FIRRTL_Type> output_ports;     // Output port name.
    map<string, FIRRTL_Type> input_streams;    // Input stream name
    map<string, FIRRTL_Type> output_streams;   // Output stream name.
    map<string, FIRRTL_Type> wires;
    map<string, FIRRTL_Type> regs;
    map<string, string> connections;
    vector<string> connection_keys; // ordered list of connections

    // components is a static member so that it contains all the components that have created.
    static map<string, Component*> components; // <modulename, Component*>
    map<string, string> instances; // <instance name, module name>
};


class TopLevel : public Component {
public:
    TopLevel(const string &name) : Component(name) {type = ComponentType::TopLvl;}

protected:
};

class LineBuffer : public Component
{
public:
    LineBuffer(const string &name) : Component(name) {type = ComponentType::Linebuffer;}

    void setStoreExtents(vector<int> e) { store_extents = e;};
    vector<int> getStoreExtents(void) { return store_extents;};

protected:
    vector<int> store_extents;
    vector<int> in_stencil_sizes;
    vector<int> out_stencil_sizes;
};

class Dispatch : public Component
{
public:
    Dispatch(const string &name) : Component(name) {type = ComponentType::Dispatcher;}

    void setStencilSizes(vector<int> e) { stencil_sizes = e;}
    void setStencilSteps(vector<int> e) { stencil_steps = e;}
    void setStoreExtents(vector<int> e) { store_extents = e;}
    void setConsumerFifoDepths(vector<int> e) { consumer_fifodepths = e;}
    void setConsumerOffsets(vector<vector<int> > e) { consumer_offsets = e;}
    void setConsumerExtents(vector<vector<int> > e) { consumer_extents = e;}
    vector<int> getStencilSizes(void) { return stencil_sizes;}
    vector<int> getStencilSteps(void) { return stencil_steps;}
    vector<int> getStoreExtents(void) { return store_extents;}
    vector<int> getConsumerFifoDepths(void) { return consumer_fifodepths;}
    vector<vector<int> > getConsumerOffsets(void) { return consumer_offsets;}
    vector<vector<int> > getConsumerExtents(void) { return consumer_extents;}
    int getNumOfConsumer(void) { return consumer_extents.size();}

protected:
    vector<int > stencil_sizes;
    vector<int > stencil_steps;
    vector<int > store_extents;

    // Consumers
    vector<int         > consumer_fifodepths;
    vector<vector<int> > consumer_offsets;
    vector<vector<int> > consumer_extents;
};

class SlaveIf : public Component
{
public:
    SlaveIf(const string &name) : Component(name) {type = ComponentType::Slaveif;}

protected:
};

class IO : public Component
{
public:
    IO(const string &name, ComponentType t) : Component(name) {type = t;}
    void setStoreExtents(vector<int> e) { store_extents = e;}
    vector<int> getStoreExtents(void) { return store_extents;}
    bool isInputIO() { return type == ComponentType::Input;}

protected:
    vector<int > store_extents;
};

class FIFO : public Component
{
public:
    FIFO(const string &name) : Component(name) {
        type  = ComponentType::Fifo;
        depth = "1";
    }

    void setDepth(string d) {depth = d;}
    string getDepth() {return depth;}

protected:
    string depth; // String type used to indicate to be back-annotated if depth is "$$"
};

class ForBlock : public Component
{
public:
    ForBlock(const string &name) : Component(name) {type = ComponentType::Forblock; indent = 0;}

    ForBlock() {type = ComponentType::Forblock;}
    void addVar(string p) {scan_vars.push_back(p);}
    void addStencilVar(string p) {stencil_vars.push_back(p);}
    void addMin(int i)    {mins.push_back(i);}
    void addMax(int i)    {maxs.push_back(i);}
    vector<string> getVars() {return scan_vars;}
    vector<string> getStencilVars() {return stencil_vars;}
    vector<int>    getMins(void) {return mins;}
    vector<int>    getMaxs(void) {return maxs;}
    void print(string s);
    vector<string> print_body();
    void open_scope();
    void close_scope(const std::string &);

protected:
    vector<string> scan_vars;
    vector<string> stencil_vars;
    vector<int> mins;
    vector<int> maxs;
    int indent;
    ostringstream oss_body;
};

}
}
#endif
