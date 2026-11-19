#pragma once
#include "Passes/ForwardPass.h"

namespace experiment
{
struct HdrRangePass
{
    static constexpr uint32_t HdrTextureSet=0; // image binding 0, sampler binding 1
    struct Inputs { madcat::graph::Resource hdr, display; };
    struct Outputs { madcat::graph::Resource color; };
    struct Declared { Inputs inputs; Outputs outputs; };
    struct Prepared { madcat::rhi::GraphicsPipelineRef pipeline; };
    struct Parameters {};

    Declared declare(madcat::graph::Builder& builder,const Inputs& input) const
    {
        builder.read(input.hdr,madcat::graph::Use::Sampled);
        return {input,{builder.write(input.display,madcat::graph::Use::Color,madcat::graph::Write::Overwrite)}};
    }
    static std::expected<Prepared,std::string> prepare(const madcat::rhi::GraphicsPipelineFactory& factory,
        const std::filesystem::path& shaderDirectory)
    {
        if (!factory) { return std::unexpected("Missing graphics preparation service"); }
        madcat::rhi::GraphicsPipelineDesc desc;
        desc.vertexShader=shaderDirectory/"vertex.spv"; desc.fragmentShader=shaderDirectory/"fragment.spv";
        desc.sampledTexture=true;
        auto pipeline=factory(desc);
        if (!pipeline) { return std::unexpected("HDR range preparation failed; build CustomPassHdrShaders then restart"); }
        return Prepared{std::move(pipeline)};
    }
    bool execute(madcat::graph::PassContext& context,const Declared& declared,
        const Prepared& prepared,const Parameters&) const
    {
        return context.beginRendering(declared.outputs.color,{},madcat::rhi::AttachmentLoad::Clear,{}) &&
            context.bindPipeline(prepared.pipeline) && context.bindSampledTexture(HdrTextureSet,declared.inputs.hdr) &&
            context.draw(3) && context.endRendering();
    }
};
}
