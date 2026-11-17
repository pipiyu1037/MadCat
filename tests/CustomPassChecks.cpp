#include "Runtime/Application.h"
#include "../examples/CustomPass/ToneMapPass.h"
#include <cstdio>
#include <stdexcept>

namespace
{
using namespace madcat;
struct FailurePass : experiment::ToneMapPass
{
    int failure;
    bool execute(graph::PassContext& context,const Declared& d,const Prepared& prepared,const Parameters& value) const
    {
        if (!context.beginRendering(d.outputs.color,{},rhi::AttachmentLoad::Clear,{}) || !context.bindPipeline(prepared.pipeline)) { return false; }
        if (failure==2) { return context.draw(3); } // missing sampled binding/constants
        if (!context.bindSampledTexture(failure==3 ? 1 : 0,d.inputs.hdr)) { return false; }
        if (failure==4) { const float bad[2]{}; return context.pushConstants(std::as_bytes(std::span(bad))); }
        if (!context.pushConstants(std::as_bytes(std::span(&value,1)))) { return false; }
        if (failure==5) { return context.draw(2); } // non-triangle count
        return context.draw(3) && context.endRendering();
    }
};
}
int main()
{
    assets::ModelAssets assets;
    render::GeometryBatch geometry;
    geometry.vertices={{{-.5f,-.5f,0},{1,0,0}},{{.5f,-.5f,0},{0,1,0}},{{0,.5f,0},{0,0,1}}};
    geometry.indices={0,1,2};
    for (int failure=0; failure<6; ++failure)
    {
        runtime::ApplicationOptions options;
        options.title="MadCat CustomPass failure checks";
        options.graphFactory=[&](graph::Graph& graph,const graph::GraphicsImports& input)
            -> std::expected<graph::GraphicsGraph,std::string> {
            rhi::GraphicsPipelineDesc desc;
            const auto directory=std::filesystem::path(u8"" MADCAT_CUSTOM_SHADERS);
            desc.vertexShader=directory/"vertex.spv"; desc.fragmentShader=directory/"fragment.spv";
            desc.sampledTexture=true; desc.pushConstantBytes=failure==0 ? 3 : 4;
            if (failure==1) { desc.colorFormat=rhi::ColorFormat::Rgba16Float; }
            auto pipeline=input.prepareGraphics(desc);
            if (!pipeline) { return std::unexpected("Injected unsupported preparation description"); }
            const auto hdr=graph.createColorTexture("HDR",{input.width,input.height});
            const auto clear=graph.addPass("Clear",passes::ClearPass{},passes::Targets{hdr,input.depth});
            const auto display=graph.addPass("InvalidDisplay",FailurePass{{},failure},
                FailurePass::Inputs{clear.outputs.color,input.color},FailurePass::Prepared{std::move(pipeline)});
            const auto present=graph.addPass("Present",passes::PresentPass{},passes::PresentPass::Inputs{display.outputs.color});
            auto plan=graph.compile();
            if (!plan) { return std::unexpected(plan.error()); }
            auto parameters=[clearNode=clear.node,displayNode=display.node,presentNode=present.node,count=plan->nodes().size()]
                (const std::array<float,4>& color,std::span<const rhi::IndexedDraw>,std::span<const rhi::IndexedDraw>) {
                std::vector<std::any> values(count);
                values[clearNode]=passes::ClearPass::Parameters{color};
                values[displayNode]=FailurePass::Parameters{};
                values[presentNode]=passes::PresentPass::Parameters{};
                return values;
            };
            return graph::GraphicsGraph{std::move(*plan),std::move(parameters)};
        };
        runtime::GraphicsCallbacks callbacks;
        bool presented=false;
        callbacks.frame=[](const runtime::FrameInfo&) -> std::expected<runtime::RenderFrame,std::string> { return runtime::RenderFrame{}; };
        callbacks.presented=[&] { presented=true; throw std::runtime_error("Invalid custom Pass was submitted"); };
        if (runtime::runGraphics(options,assets,geometry,callbacks)==0 || presented)
        { std::fprintf(stderr,"CustomPass check failed: invalid case %d submitted.\n",failure); return 1; }
        std::fprintf(stderr,"CustomPass check passed: failure %d rejected and retired.\n",failure);
    }
    std::fprintf(stderr,"CustomPass checks: 6 failure boundaries passed.\n");
    return 0;
}
