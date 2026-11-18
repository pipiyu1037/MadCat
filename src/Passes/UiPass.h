#pragma once
#include "RenderGraph/RenderGraph.h"

namespace madcat::passes
{
// UI is a resource consumer and a preserving color writer, before Present.
struct UiPass
{
    struct Inputs { graph::Resource color, font; };
    struct Outputs { graph::Resource color; };
    struct Declared { Inputs inputs; Outputs outputs; };
    struct Parameters {};
    Declared declare(graph::Builder& builder, const Inputs& input) const
    {
        builder.read(input.font,graph::Use::Sampled);
        return {input,{builder.write(input.color,graph::Use::Color,graph::Write::Preserve)}};
    }
    bool execute(graph::PassContext& context, const Declared& declared, const Parameters&) const
    {
        return context.beginRendering(declared.outputs.color,{},rhi::AttachmentLoad::Load,{}) &&
            context.drawUi(declared.inputs.font) && context.endRendering();
    }
};
}
