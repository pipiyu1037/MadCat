#include "Runtime/Application.h"
#include "../examples/CustomPass/DisplayGraph.h"
#include <imgui.h>
#include <cstdio>
#include <stdexcept>

int main()
{
    using namespace madcat;
    assets::ModelAssets assets;
    render::GeometryBatch geometry;
    geometry.vertices={{{-.5f,-.5f,0},{1,0,0}},{{.5f,-.5f,0},{0,1,0}},{{0,.5f,0},{0,0,1}}};
    geometry.indices={0,1,2};
    experiment::ToneMapPass::Parameters display;
    experiment::DisplaySettings effective;
    bool awaiting=false,finished=false,failed=false;
    size_t next=0; int heldFrames=0;
    auto makeGraph=[&](experiment::DisplaySettings settings) -> graph::GraphicsGraphFactory {
        auto normal=experiment::makeDisplayGraph(display,std::filesystem::path(u8"" MADCAT_EXAMPLE_SHADERS),settings);
        return [&,normal=std::move(normal),settings](graph::Graph& graph,const graph::GraphicsImports& input)
            -> std::expected<graph::GraphicsGraph,std::string> {
            auto result=normal(graph,input);
            if (result) {
                auto parameters=std::move(result->parameters);
                result->parameters=[&,parameters=std::move(parameters),settings](const auto& color,auto draws,auto shadows) {
                    // Every image plan must agree with the last accepted request.
                    // This catches partial publication and stale plans after failure.
                    if (settings!=effective) { throw std::runtime_error("Partially published or stale graph executed"); }
                    return parameters(color,draws,shadows);
                };
            }
            return result;
        };
    };
    using enum experiment::DisplayView;
    struct Case { uint64_t id; experiment::DisplaySettings settings; int fault; };
    const std::array cases{Case{1,{false,Shadow},0},Case{2,{true,Shadow},1},Case{3,{true,HdrRange},2},Case{4,{true,Final},3},
        Case{0,{true,Final},4},Case{5,{true,Final},5},Case{6,{true,Depth},0},Case{7,{true,HdrRange},0},Case{8,{true,Final},0}};
    runtime::ApplicationOptions options;
    options.title="MadCat Graph switch checks";
    options.forwardColorFormat=rhi::ColorFormat::Rgba16Float;
    options.prepareDepthView=true;
    options.prepareShadow=true;
    options.graphFactory=makeGraph(effective);
    runtime::GraphicsCallbacks callbacks;
    callbacks.ui=[] { ImGui::Begin("Graph switching"); ImGui::TextUnformatted("Automatic failure/recovery checks"); ImGui::End(); };
    callbacks.frame=[](const runtime::FrameInfo&) -> std::expected<runtime::RenderFrame,std::string> { return runtime::RenderFrame{}; };
    callbacks.graphRequest=[&]() -> std::optional<runtime::GraphRequest> {
        if (next==cases.size() || heldFrames<6 || awaiting) { return {}; }
        awaiting=true;
        const auto test=cases[next];
        auto factory=makeGraph(test.settings);
        if (test.fault==1 || test.fault==2) {
            factory=[normal=std::move(factory),calls=0,fault=test.fault](graph::Graph& graph,const graph::GraphicsImports& input) mutable
                -> std::expected<graph::GraphicsGraph,std::string> {
                if (++calls==2) {
                    if (fault==2) { throw std::runtime_error("Injected late candidate exception"); }
                    return std::unexpected("Injected second-image rejection");
                }
                return normal(graph,input);
            };
        } else if (test.fault==3) {
            factory=[](graph::Graph& graph,const graph::GraphicsImports& input) -> std::expected<graph::GraphicsGraph,std::string> {
                const auto clear=graph.addPass("MissingPresent",passes::ClearPass{},passes::Targets{input.color,input.depth});
                auto plan=graph.compile();
                if (!plan) { return std::unexpected(plan.error()); }
                return graph::GraphicsGraph{std::move(*plan),[node=clear.node](const auto& color,auto,auto){
                    std::vector<std::any> values(1); values[node]=passes::ClearPass::Parameters{color}; return values;
                }};
            };
        } else if (test.fault==5) { factory={}; }
        return runtime::GraphRequest{test.id,std::move(factory)};
    };
    callbacks.graphUpdated=[&](const runtime::GraphUpdate& result) {
        const auto test=cases[next];
        if (!awaiting || result.id!=test.id || result.applied!=(test.fault==0) ||
            (!result.applied && result.diagnostic.empty())) { failed=true; throw std::runtime_error("Wrong graph request outcome"); }
        if (test.fault==1 && result.diagnostic.find("second-image rejection")==std::string::npos)
        { failed=true; throw std::runtime_error("Late rejection hit wrong boundary"); }
        if (test.fault==2 && result.diagnostic.find("late candidate exception")==std::string::npos)
        { failed=true; throw std::runtime_error("Exception hit wrong boundary"); }
        if (result.applied) { effective=test.settings; }
        std::fprintf(stderr,"Graph switch check: case %zu %s; effective Forward %d, view %s.\n",next,
            result.applied ? "applied" : "retained",effective.forwardEnabled,experiment::displayViewName(effective.view));
        ++next; heldFrames=0; awaiting=false;
    };
    callbacks.presented=[&] {
        ++heldFrames;
        if (next==cases.size() && heldFrames==6) {
            finished=true;
            std::fprintf(stderr,"Graph switch checks: 9 requests passed; ready to close.\n");
        }
    };
    const int result=runtime::runGraphics(options,assets,geometry,callbacks);
    if (result || !finished || failed || ImGui::GetCurrentContext()) { return 1; }
    std::fprintf(stderr,"Graph switch checks: cleanup passed.\n");
    return 0;
}
