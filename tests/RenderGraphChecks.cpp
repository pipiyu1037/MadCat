#include "Passes/ForwardPass.h"
#include <cstdio>
#include <stdexcept>

namespace
{
using namespace madcat;
void check(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
graph::Resource texture(graph::Graph& g, uint64_t identity=1, bool valid=false)
{ return g.import("color",identity,graph::Kind::Color,valid ? graph::Use::Sampled : graph::Use::None,valid); }
struct Commands : rhi::GraphicsCommands
{
    int begins=0, ends=0, draws=0;
    bool beginRendering(uint32_t,uint32_t,rhi::AttachmentLoad,const std::array<float,4>&) override { ++begins; return true; }
    bool drawIndexed(uint32_t,uint32_t,uint32_t,std::span<const uint32_t>,const rhi::IndexedDraw&) override { ++draws; return true; }
    bool endRendering() override { ++ends; return true; }
};
void versionsAndOrdering()
{
    graph::Graph g;
    auto image=texture(g);
    const auto reader=g.addNode("late-added old-content reader"), overwrite=g.addNode("overwrite"), producer=g.addNode("producer");
    auto first=g.builder(producer).write(image,graph::Use::Color,graph::Write::Overwrite);
    auto second=g.builder(overwrite).write(first,graph::Use::Color,graph::Write::Overwrite);
    g.builder(reader).read(first,graph::Use::Sampled);
    auto finalReader=g.addNode("final reader"); g.builder(finalReader).read(second,graph::Use::Sampled);
    auto plan=g.compile();
    check(plan && plan->order()==std::vector<uint32_t>{producer,reader,overwrite,finalReader},"Producer/old reader/overwrite order wrong");
}
void diagnostics()
{
    {
        graph::Graph g; auto image=texture(g); auto node=g.addNode("missing input"); g.builder(node).read(image,graph::Use::Sampled);
        auto result=g.compile(); check(!result && result.error().find("uninitialized color v0")!=std::string::npos,"Uninitialized read lacks diagnostic");
    }
    {
        graph::Graph g; auto image=texture(g); auto node=g.addNode("load without clear");
        (void)g.builder(node).write(image,graph::Use::Color,graph::Write::Preserve);
        check(!g.compile(),"Preserve uninitialized accepted");
    }
    {
        graph::Graph g; auto image=texture(g,1,true);
        (void)g.builder(g.addNode("first")).write(image,graph::Use::Color,graph::Write::Overwrite);
        (void)g.builder(g.addNode("second")).write(image,graph::Use::Color,graph::Write::Overwrite);
        auto result=g.compile(); check(!result && result.error().find("Multiple writers")!=std::string::npos,"Branch writers accepted");
    }
    {
        graph::Graph g; auto image=texture(g); const auto a=g.addNode("writer"), b=g.addNode("reader");
        image=g.builder(a).write(image,graph::Use::Color,graph::Write::Overwrite); g.builder(b).read(image,graph::Use::Sampled); g.dependsOn(a,b);
        auto result=g.compile(); check(!result && result.error().find("writer")!=std::string::npos && result.error().find("reader")!=std::string::npos,"Cycle not diagnosed");
    }
    {
        graph::Graph a,b; const auto image=texture(a);
        b.builder(b.addNode("foreign")).read(image,graph::Use::Sampled); check(!b.compile(),"Foreign handle accepted");
        graph::Graph c; c.builder(c.addNode("missing")).read({},graph::Use::Vertex); check(!c.compile(),"Missing handle accepted");
        graph::Graph wrongType; auto color=texture(wrongType,1,true);
        wrongType.builder(wrongType.addNode("wrong type")).read(color,graph::Use::Vertex); check(!wrongType.compile(),"Texture treated as vertex buffer");
    }
}
void duplicateIdentity()
{
    graph::Graph g; auto first=texture(g); auto alias=texture(g);
    check(first==alias,"Duplicate import hid physical identity");
    (void)g.builder(g.addNode("write1")).write(first,graph::Use::Color,graph::Write::Overwrite);
    (void)g.builder(g.addNode("write2")).write(alias,graph::Use::Color,graph::Write::Overwrite);
    check(!g.compile(),"Aliased writes accepted");
    graph::Graph conflicting; (void)texture(conflicting); (void)texture(conflicting,1,true);
    check(!conflicting.compile(),"Inconsistent import state accepted");
}
void executionAndFailure()
{
    graph::Graph g;
    auto color=texture(g), depth=g.import("depth",2,graph::Kind::Depth,graph::Use::None,false);
    const auto clear=g.addPass("Clear",passes::ClearPass{},passes::Targets{color,depth});
    const auto present=g.addPass("Present",passes::PresentPass{},passes::PresentPass::Inputs{clear.outputs.color});
    auto plan=g.compile(); check(bool(plan),"Clear/Present compile failed");
    std::vector<std::any> params(2); params[clear.node]=passes::ClearPass::Parameters{{.1f,.2f,.3f,1}}; params[present.node]=passes::PresentPass::Parameters{};
    std::vector<graph::Use> initial{graph::Use::None,graph::Use::None};
    std::vector<graph::Barrier> barriers; Commands commands;
    auto result=plan->record(commands,params,initial,[&](auto b){ barriers.push_back(b); return true; });
    check(result && commands.begins==1 && commands.ends==1 && barriers.size()==3,"Clear/Present execution failed");
    check((*result)[0]==graph::Use::Present && initial[0]==graph::Use::None,"Recording mutated external state");
    check(barriers[0].before==graph::Use::None && barriers.back().after==graph::Use::Present,"Wrong boundary transitions");
    params[present.node]=42;
    result=plan->record(commands,params,initial,[](auto){return true;});
    check(!result && result.error().starts_with("Present"),"Wrong terminal parameters accepted or no Pass diagnostic");
    check(initial[0]==graph::Use::None,"Failed recording advanced external state");
    params[present.node]=passes::PresentPass::Parameters{};
    check(!plan->record(commands,params,initial,[](auto){return false;}),"Barrier failure ignored");
    struct IgnoredFailure : passes::ClearPass
    {
        bool execute(graph::PassContext& context,const Declared& d,const Parameters&) const
        { (void)context.require(d.outputs.color,graph::Use::Sampled); return true; }
    };
    graph::Graph bad;
    (void)bad.addPass("ignored failure",IgnoredFailure{},passes::Targets{texture(bad),bad.import("depth",2,graph::Kind::Depth,graph::Use::None,false)});
    auto invalid=bad.compile();
    check(invalid && !invalid->record(commands,std::vector<std::any>{passes::ClearPass::Parameters{}},initial,[](auto){return true;}),"Pass ignored an access violation and continued");
}
void accessAndSnapshots()
{
    graph::Graph g; auto color=texture(g); auto depth=g.import("depth",2,graph::Kind::Depth,graph::Use::None,false);
    auto clear=g.addPass("Clear",passes::ClearPass{},passes::Targets{color,depth});
    auto plan=g.compile(); check(bool(plan),"Snapshot compile failed");
    Commands commands;
    graph::PassContext context(plan->nodes()[clear.node].accesses,commands);
    check(!context.require(color,graph::Use::Sampled),"Undeclared/old-version access accepted");
    check(!context.beginRendering(clear.outputs.color,clear.outputs.depth,rhi::AttachmentLoad::Load,{}),"Wrong load operation accepted");
    // A candidate's failure must not mutate the already compiled active plan.
    (void)g.builder(g.addNode("conflict")).write(color,graph::Use::Color,graph::Write::Overwrite);
    check(!g.compile(),"Candidate conflict accepted");
    std::vector<std::any> params{passes::ClearPass::Parameters{{0,0,0,1}}};
    check(bool(plan->record(commands,params,std::vector<graph::Use>{graph::Use::None,graph::Use::None},[](auto){return true;})),"Candidate invalidated active plan");
}
void forwardSynchronization()
{
    graph::Graph g;
    auto color=texture(g), depth=g.import("depth",2,graph::Kind::Depth,graph::Use::None,false);
    auto vertices=g.import("vertices",3,graph::Kind::Buffer,graph::Use::HostWrite,true);
    auto indices=g.import("indices",4,graph::Kind::Buffer,graph::Use::HostWrite,true);
    auto lighting=g.import("lighting",5,graph::Kind::Buffer,graph::Use::HostWrite,true);
    auto sampled=texture(g,6,true);
    auto clear=g.addPass("Clear",passes::ClearPass{},passes::Targets{color,depth});
    auto forward=g.addPass("Forward",passes::ForwardPass{},passes::ForwardPass::Inputs{clear.outputs,vertices,indices,lighting,{sampled}});
    auto present=g.addPass("Present",passes::PresentPass{},passes::PresentPass::Inputs{forward.outputs.color});
    auto plan=g.compile(); check(bool(plan),"Forward compile failed");
    std::vector<std::any> parameters(3); parameters[clear.node]=passes::ClearPass::Parameters{{0,0,0,1}};
    rhi::IndexedDraw draw; draw.indexCount=3;
    parameters[forward.node]=passes::ForwardPass::Parameters{{draw}};
    parameters[present.node]=passes::PresentPass::Parameters{};
    std::vector<graph::Use> initial{graph::Use::Present,graph::Use::Depth,graph::Use::HostWrite,graph::Use::HostWrite,graph::Use::HostWrite,graph::Use::Sampled};
    std::vector<graph::Barrier> barriers; Commands commands;
    auto result=plan->record(commands,parameters,initial,[&](auto value){barriers.push_back(value);return true;});
    check(result && commands.begins==2 && commands.ends==2 && commands.draws==1,"Forward did not load/draw both attachments");
    bool colorWaw=false,depthWaw=false,uniform=false;
    for(const auto b:barriers){
        colorWaw|=b.before==graph::Use::Color && b.after==graph::Use::Color;
        depthWaw|=b.before==graph::Use::Depth && b.after==graph::Use::Depth;
        uniform|=b.before==graph::Use::HostWrite && b.after==graph::Use::Uniform;
    }
    check(colorWaw && depthWaw && uniform,"Missing WAW or host-to-uniform synchronization");
    check((*result)[0]==graph::Use::Present && (*result)[1]==graph::Use::Depth,"Wrong exported attachment states");
    graph::PassContext context(plan->nodes()[forward.node].accesses,commands);
    check(!context.require(sampled,graph::Use::Color),"Sampled declaration granted write access");
    check(!context.require(lighting,graph::Use::Vertex),"Uniform declaration granted vertex access");
}
struct ParameterProbe
{
    struct Inputs {};
    struct Outputs {};
    struct Declared { Outputs outputs; };
    using Parameters=int;
    std::vector<int>* seen;
    std::any* mutate=nullptr;
    Declared declare(graph::Builder&,const Inputs&) const { return {}; }
    bool execute(graph::PassContext&,const Declared&,int value) const
    { seen->push_back(value); if(mutate){*mutate=999;} return true; }
};
void parameterIsolation()
{
    graph::Graph g; std::vector<std::any> parameters{7,8}; std::vector<int> seen;
    (void)g.addPass("mutator",ParameterProbe{&seen,&parameters[1]},ParameterProbe::Inputs{});
    (void)g.addPass("observer",ParameterProbe{&seen},ParameterProbe::Inputs{});
    auto plan=g.compile(); Commands commands;
    check(plan && plan->record(commands,parameters,{},[](auto){return true;}),"Parameter probe failed");
    check(seen==std::vector<int>{7,8} && std::any_cast<int>(parameters[1])==999,"Frame observed a parameter edit after recording started");
}
}
int main()
{
    try { versionsAndOrdering(); diagnostics(); duplicateIdentity(); executionAndFailure(); accessAndSnapshots(); forwardSynchronization(); parameterIsolation(); std::puts("Render Graph checks: 7 groups passed."); return 0; }
    catch(const std::exception& error) { std::fprintf(stderr,"Render Graph checks failed: %s\n",error.what()); return 1; }
}
