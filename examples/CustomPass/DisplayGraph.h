#pragma once
#include "ToneMapPass.h"
#include "HdrRangePass.h"
#include "Passes/DepthViewPass.h"
#include "Passes/UiPass.h"

namespace experiment
{
enum class DisplayView { Final, Depth, HdrRange };
struct DisplaySettings
{
    bool forwardEnabled=true;
    DisplayView view=DisplayView::Final;
    bool operator==(const DisplaySettings&) const = default;
};
inline const char* displayViewName(DisplayView view)
{ return view==DisplayView::HdrRange ? "hdr-range" : view==DisplayView::Depth ? "depth" : "final"; }

// Structural settings are captured by value. Parameters remain owned by the
// experiment, then copied at the frame's record boundary. Do not outlive run().
inline madcat::graph::GraphicsGraphFactory makeDisplayGraph(const ToneMapPass::Parameters& display,
    std::filesystem::path shaderRoot, DisplaySettings settings={})
{
    return [&display,shaderRoot=std::move(shaderRoot),settings](madcat::graph::Graph& graph,
        const madcat::graph::GraphicsImports& input) -> std::expected<madcat::graph::GraphicsGraph,std::string> {
        using namespace madcat;
        if (!input.presentationSrgb) { return std::unexpected("CustomPass display Shader requires an sRGB presentation attachment"); }
        const auto hdr=graph.createColorTexture("SceneHDR",{input.width,input.height});
        const auto depth=graph.createDepthTexture("SceneDepth",{input.width,input.height});
        const auto clear=graph.addPass("ClearHDR",passes::ClearPass{},passes::Targets{hdr,depth});
        auto targets=clear.outputs;
        uint32_t forwardNode=UINT32_MAX;
        if (settings.forwardEnabled)
        {
            const auto forward=graph.addPass("ForwardHDR",passes::ForwardPass{},passes::ForwardPass::Inputs{
                clear.outputs,input.vertices,input.indices,input.lighting,input.textures});
            forwardNode=forward.node; targets=forward.outputs;
        }
        uint32_t displayNode;
        graph::Resource displayed;
        if (settings.view==DisplayView::Depth)
        {
            const auto viewer=graph.addPass("DepthView",passes::DepthViewPass{},passes::DepthViewPass::Inputs{input.color,targets.depth});
            displayNode=viewer.node; displayed=viewer.outputs.color;
        }
        else if (settings.view==DisplayView::HdrRange)
        {
            auto prepared=HdrRangePass::prepare(input.prepareGraphics,shaderRoot/"custom-hdr-range");
            if (!prepared) { return std::unexpected(prepared.error()); }
            const auto viewer=graph.addPass("HDRRange",HdrRangePass{},HdrRangePass::Inputs{targets.color,input.color},std::move(*prepared));
            displayNode=viewer.node; displayed=viewer.outputs.color;
        }
        else
        {
            auto prepared=ToneMapPass::prepare(input.prepareGraphics,shaderRoot/"custom-display");
            if (!prepared) { return std::unexpected(prepared.error()); }
            const auto tone=graph.addPass("ToneMap",ToneMapPass{},ToneMapPass::Inputs{targets.color,input.color},std::move(*prepared));
            displayNode=tone.node; displayed=tone.outputs.color;
        }
        const auto ui=graph.addPass("UI",passes::UiPass{},passes::UiPass::Inputs{displayed,input.uiFont});
        const auto present=graph.addPass("Present",passes::PresentPass{},passes::PresentPass::Inputs{ui.outputs.color});
        auto plan=graph.compile();
        if (!plan) { return std::unexpected(plan.error()); }
        auto parameters=[&display,view=settings.view,clearNode=clear.node,forwardNode,displayNode,uiNode=ui.node,
            presentNode=present.node,count=plan->nodes().size()](const std::array<float,4>& color,
                std::span<const rhi::IndexedDraw> draws,std::span<const rhi::IndexedDraw>) {
            std::vector<std::any> values(count);
            values[clearNode]=passes::ClearPass::Parameters{color};
            if (forwardNode!=UINT32_MAX) { values[forwardNode]=passes::ForwardPass::Parameters{{draws.begin(),draws.end()}}; }
            if (view==DisplayView::Depth) { values[displayNode]=passes::DepthViewPass::Parameters{}; }
            else if (view==DisplayView::HdrRange) { values[displayNode]=HdrRangePass::Parameters{}; }
            else { values[displayNode]=display; }
            values[uiNode]=passes::UiPass::Parameters{};
            values[presentNode]=passes::PresentPass::Parameters{};
            return values;
        };
        return graph::GraphicsGraph{std::move(*plan),std::move(parameters)};
    };
}
}
