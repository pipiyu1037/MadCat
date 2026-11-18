#include "Runtime/Application.h"
#include "Passes/ForwardPass.h"
#include "Passes/UiPass.h"
#include <imgui.h>
#include <cstdio>
#include <stdexcept>

namespace
{
using namespace madcat;
struct UndeclaredUi : passes::UiPass
{
    Declared declare(graph::Builder& builder,const Inputs& input) const
    { return {input,{builder.write(input.color,graph::Use::Color,graph::Write::Preserve)}}; }
};
}
int main()
{
    assets::ModelAssets assets;
    render::GeometryBatch geometry;
    geometry.vertices={{{-.5f,-.5f,0},{1,0,0}},{{.5f,-.5f,0},{0,1,0}},{{0,.5f,0},{0,0,1}}};
    geometry.indices={0,1,2};
    for (int failure=0;failure<5;++failure)
    {
        runtime::ApplicationOptions options;
        options.title="MadCat UI failure checks";
        options.graphFactory=[&](graph::Graph& graph,const graph::GraphicsImports& input)
            -> std::expected<graph::GraphicsGraph,std::string> {
            if (failure==4) { return std::unexpected("Injected UI graph preparation failure"); }
            const auto clear=graph.addPass("Clear",passes::ClearPass{},passes::Targets{input.color,input.depth});
            uint32_t uiNode=0;
            graph::Resource output;
            if (failure==2) {
                const auto ui=graph.addPass("UndeclaredUI",UndeclaredUi{},UndeclaredUi::Inputs{clear.outputs.color,input.uiFont});
                uiNode=ui.node; output=ui.outputs.color;
            } else {
                const auto ui=graph.addPass("UI",passes::UiPass{},passes::UiPass::Inputs{clear.outputs.color,input.uiFont});
                uiNode=ui.node; output=ui.outputs.color;
            }
            uint32_t duplicate=UINT32_MAX;
            if (failure==3)
            {
                const auto repeated=graph.addPass("DuplicateUI",passes::UiPass{},passes::UiPass::Inputs{output,input.uiFont});
                output=repeated.outputs.color; duplicate=repeated.node;
            }
            const auto present=graph.addPass("Present",passes::PresentPass{},passes::PresentPass::Inputs{output});
            auto plan=graph.compile();
            if (!plan) { return std::unexpected(plan.error()); }
            auto parameters=[clearNode=clear.node,uiNode,presentNode=present.node,duplicate,count=plan->nodes().size()]
                (const std::array<float,4>& color,std::span<const rhi::IndexedDraw>,std::span<const rhi::IndexedDraw>) {
                std::vector<std::any> values(count);
                values[clearNode]=passes::ClearPass::Parameters{color};
                values[uiNode]=passes::UiPass::Parameters{};
                if (duplicate!=UINT32_MAX) { values[duplicate]=passes::UiPass::Parameters{}; }
                values[presentNode]=passes::PresentPass::Parameters{};
                return values;
            };
            return graph::GraphicsGraph{std::move(*plan),std::move(parameters)};
        };
        runtime::GraphicsCallbacks callbacks;
        bool presented=false;
        callbacks.frame=[](const runtime::FrameInfo&) -> std::expected<runtime::RenderFrame,std::string> { return runtime::RenderFrame{}; };
        callbacks.ui=[&] {
            ImGui::Begin("UI check");
            if (failure==0) { throw std::runtime_error("Injected UI callback failure"); }
            ImGui::TextUnformatted("Parameter controls");
            if (failure==1) { ImGui::GetForegroundDrawList()->AddImage(ImTextureRef(ImTextureID(42)),{10,10},{26,26}); }
            ImGui::End();
        };
        callbacks.presented=[&] { presented=true; throw std::runtime_error("Invalid UI frame was submitted"); };
        if (runtime::runGraphics(options,assets,geometry,callbacks)==0 || presented || ImGui::GetCurrentContext())
        { std::fprintf(stderr,"UI check failed: case %d submitted or leaked context.\n",failure); return 1; }
        std::fprintf(stderr,"UI check passed: failure %d rejected and retired.\n",failure);
    }
    std::fprintf(stderr,"UI checks: 5 failure boundaries passed.\n");
    return 0;
}
