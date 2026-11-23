#pragma once
#include "Passes/ForwardPass.h"
#include <cmath>

namespace experiment
{
struct ToneMapPass
{
    static constexpr uint32_t HdrTextureSet = 0; // image binding 0, sampler binding 1
    struct Inputs { madcat::graph::Resource hdr, display; };
    struct Outputs { madcat::graph::Resource color; };
    struct Declared { Inputs inputs; Outputs outputs; };
    struct Prepared { madcat::rhi::GraphicsPipelineRef pipeline; };
    struct Parameters { float exposureEV = 0; };
    static_assert(sizeof(Parameters)==4);

    Declared declare(madcat::graph::Builder& builder, const Inputs& input) const
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
        desc.sampledTexture=true; desc.pushConstantBytes=sizeof(Parameters);
        auto pipeline=factory(desc);
        if (!pipeline) { return std::unexpected("ToneMap pipeline preparation failed"); }
        return Prepared{std::move(pipeline)};
    }
    bool execute(madcat::graph::PassContext& context, const Declared& declared,
        const Prepared& prepared, const Parameters& parameters) const
    {
        if (!std::isfinite(parameters.exposureEV) || std::abs(parameters.exposureEV)>16) { return false; }
        return context.beginRendering(declared.outputs.color,{},madcat::rhi::AttachmentLoad::Clear,{}) &&
            context.bindPipeline(prepared.pipeline) && context.bindSampledTexture(HdrTextureSet,declared.inputs.hdr) &&
            context.pushConstants(std::as_bytes(std::span(&parameters,1))) && context.draw(3) && context.endRendering();
    }
};
}
