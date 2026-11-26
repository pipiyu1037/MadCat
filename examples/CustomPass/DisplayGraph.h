#pragma once
#include "ToneMapPass.h"
#include "HdrRangePass.h"
#include "GeometryComputePass.h"
#include "RayQueryPass.h"
#include "Passes/DepthViewPass.h"
#include "Passes/ShadowPass.h"
#include "Passes/UiPass.h"

namespace experiment
{
enum class DisplayView { Final, Depth, HdrRange, Shadow, BaseColor };
struct DisplaySettings
{
    bool forwardEnabled=true;
    DisplayView view=DisplayView::Final;
    bool normalMapping=true;
    bool operator==(const DisplaySettings&) const = default;
};
inline const char* displayViewName(DisplayView view)
{ return view==DisplayView::BaseColor ? "base-color" : view==DisplayView::Shadow ? "shadow" : view==DisplayView::HdrRange ? "hdr-range" : view==DisplayView::Depth ? "depth" : "final"; }

// Structural settings are captured by value. Parameters remain owned by the
// experiment, then copied at the frame's record boundary. Do not outlive run().
inline madcat::graph::GraphicsGraphFactory makeDisplayGraph(const ToneMapPass::Parameters& display,
    std::filesystem::path shaderRoot, DisplaySettings settings={},std::optional<GeometryReference> reference={},std::shared_ptr<RayProbeState> probe={})
{
    return [&display,shaderRoot=std::move(shaderRoot),settings,reference,probe=std::move(probe)](madcat::graph::Graph& graph,
        const madcat::graph::GraphicsImports& input) -> std::expected<madcat::graph::GraphicsGraph,std::string> {
        using namespace madcat;
        if (!input.presentationSrgb) { return std::unexpected("CustomPass display Shader requires an sRGB presentation attachment"); }
        if (probe && !reference) { return std::unexpected("Ray Query experiment requires its startup geometry reference"); }
        uint32_t computeNode=UINT32_MAX,readbackNode=UINT32_MAX,resultSlot=UINT32_MAX;
        if (reference)
        {
            if (!input.storageGeometry || !input.prepareCompute) { return std::unexpected("Geometry compute requires Storage geometry and compute preparation"); }
            auto pipeline=input.prepareCompute({std::filesystem::path(u8"" MADCAT_EXAMPLE_SHADERS)/"custom-geometry/compute.spv",2,1});
            if (!pipeline) { return std::unexpected("Geometry compute pipeline preparation failed; build CustomPassComputeShaders"); }
            const auto result=graph.createStorageBuffer("GeometryResult",{16});
            const auto compute=graph.addPass("GeometryCompute",GeometryComputePass{},GeometryComputePass::Inputs{
                input.vertices,input.indices,result},std::move(pipeline));
            const auto readback=graph.addPass("GeometryReadback",GeometryReadbackPass{},GeometryReadbackPass::Inputs{compute.outputs.result});
            computeNode=compute.node; readbackNode=readback.node; resultSlot=compute.outputs.result.index;
        }
        const auto ray=addRayGraph(graph,input,probe);
        if (!ray) { return std::unexpected(ray.error()); }
        passes::Targets targets;
        uint32_t clearNode=UINT32_MAX, forwardNode=UINT32_MAX, shadowNode=UINT32_MAX;
        if (settings.view==DisplayView::Shadow)
        {
            const auto depth=graph.createDepthTexture("DirectionalShadow",{1024,1024});
            const auto shadow=graph.addPass("Shadow",passes::ShadowPass{},passes::ShadowPass::Inputs{
                depth,input.vertices,input.indices,input.textures});
            shadowNode=shadow.node; targets.depth=shadow.outputs.depth;
        }
        else
        {
            const auto hdr=graph.createColorTexture("SceneHDR",{input.width,input.height});
            const auto depth=graph.createDepthTexture("SceneDepth",{input.width,input.height});
            const auto clear=graph.addPass("ClearHDR",passes::ClearPass{},passes::Targets{hdr,depth});
            clearNode=clear.node; targets=clear.outputs;
            if (settings.forwardEnabled)
            {
                const auto forward=graph.addPass("ForwardHDR",passes::ForwardPass{},passes::ForwardPass::Inputs{
                    clear.outputs,input.vertices,input.indices,input.lighting,input.textures});
                forwardNode=forward.node; targets=forward.outputs;
            }
        }
        uint32_t displayNode;
        graph::Resource displayed;
        if (settings.view==DisplayView::Depth || settings.view==DisplayView::Shadow)
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
        auto parameters=[&display,view=settings.view,normalMapping=settings.normalMapping,ray=*ray,computeNode,readbackNode,clearNode,forwardNode,shadowNode,displayNode,uiNode=ui.node,
            presentNode=present.node,count=plan->nodes().size()](const std::array<float,4>& color,
                std::span<const rhi::IndexedDraw> draws,std::span<const rhi::IndexedDraw> shadowDraws) {
            std::vector<std::any> values(count);
            ray.parameters(values,draws);
            if (computeNode!=UINT32_MAX) { values[computeNode]=GeometryComputePass::Parameters{}; values[readbackNode]=GeometryReadbackPass::Parameters{}; }
            if (clearNode!=UINT32_MAX) { values[clearNode]=passes::ClearPass::Parameters{color}; }
            if (forwardNode!=UINT32_MAX) {
                passes::ForwardPass::Parameters forward{{draws.begin(),draws.end()}};
                if (view==DisplayView::BaseColor) { for (auto& draw:forward.draws) { draw.transform.view=rhi::GeometryView::Material; } }
                if (!normalMapping) { for (auto& draw:forward.draws) { draw.transform.attributes &= ~uint32_t(rhi::HasNormalTexture); } }
                values[forwardNode]=std::move(forward);
            }
            if (shadowNode!=UINT32_MAX) { values[shadowNode]=passes::ShadowPass::Parameters{{shadowDraws.begin(),shadowDraws.end()}}; }
            if (view==DisplayView::Depth || view==DisplayView::Shadow) { values[displayNode]=passes::DepthViewPass::Parameters{}; }
            else if (view==DisplayView::HdrRange) { values[displayNode]=HdrRangePass::Parameters{}; }
            else { values[displayNode]=display; }
            values[uiNode]=passes::UiPass::Parameters{};
            values[presentNode]=passes::PresentPass::Parameters{};
            return values;
        };
        graph::GraphicsGraph result{std::move(*plan),std::move(parameters)};
        if (reference) {
            result.completed=[expected=*reference,resultSlot,ray=*ray,reported=false](std::span<const rhi::BufferReadback> readbacks) mutable {
                const auto found=std::ranges::find_if(readbacks,[&](const auto& r) { return r.slot==resultSlot; });
                if (found==readbacks.end() || found->bytes.size()!=sizeof(GeometryReference)) { return false; }
                GeometryReference actual; std::memcpy(actual.data(),found->bytes.data(),sizeof(actual));
                if (actual!=expected) {
                    std::fprintf(stderr,"CustomPass failed: compute geometry mismatch; actual %u/%u/%08x/%08x, expected %u/%u/%08x/%08x.\n",
                        actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]); return false;
                }
                if (!reported) { std::fprintf(stderr,"CustomPass: compute geometry verified; %u vertex bytes, %u index bytes, hashes %08x/%08x.\n",
                    actual[0],actual[1],actual[2],actual[3]); reported=true; }
                return ray.completed(readbacks);
            };
        }
        return result;
    };
}
}
