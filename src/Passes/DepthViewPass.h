#pragma once
#include "ForwardPass.h"

namespace madcat::passes
{
struct DepthViewPass
{
    struct Inputs { graph::Resource color, depth; };
    struct Outputs { graph::Resource color; };
    struct Declared { Inputs inputs; Outputs outputs; };
    struct Parameters {};
    Declared declare(graph::Builder& builder, const Inputs& input) const
    {
        builder.read(input.depth, graph::Use::Sampled);
        return {input, {builder.write(input.color, graph::Use::Color, graph::Write::Overwrite)}};
    }
    bool execute(graph::PassContext& context, const Declared& declared, const Parameters&) const
    {
        return context.beginRendering(declared.outputs.color, {}, rhi::AttachmentLoad::Clear, {}) &&
            context.displayDepth(declared.inputs.depth) && context.endRendering();
    }
};

inline std::expected<graph::GraphicsGraph,std::string> buildDepthViewGraph(graph::Graph& graph, const graph::GraphicsImports& imports)
{
    const auto depth = graph.createDepthTexture("ViewerDepth", {imports.width, imports.height});
    const auto clear = graph.addPass("Clear", ClearPass{}, Targets{imports.color, depth});
    auto targets = clear.outputs;
    uint32_t forwardNode = UINT32_MAX;
    if (imports.vertices.graph)
    {
        const auto forward = graph.addPass("Forward", ForwardPass{}, ForwardPass::Inputs{
            targets, imports.vertices, imports.indices, imports.lighting, imports.textures});
        forwardNode = forward.node; targets = forward.outputs;
    }
    const auto display = graph.addPass("DepthView", DepthViewPass{}, DepthViewPass::Inputs{targets.color, targets.depth});
    const auto present = graph.addPass("Present", PresentPass{}, PresentPass::Inputs{display.outputs.color});
    auto plan = graph.compile();
    if (!plan) { return std::unexpected(plan.error()); }
    auto parameters = [clearNode=clear.node, forwardNode, displayNode=display.node, presentNode=present.node,
        count=plan->nodes().size()](const std::array<float,4>& color, std::span<const rhi::IndexedDraw> draws) {
        std::vector<std::any> values(count);
        values[clearNode] = ClearPass::Parameters{color};
        if (forwardNode != UINT32_MAX) { values[forwardNode] = ForwardPass::Parameters{{draws.begin(),draws.end()}}; }
        values[displayNode] = DepthViewPass::Parameters{};
        values[presentNode] = PresentPass::Parameters{};
        return values;
    };
    return graph::GraphicsGraph{std::move(*plan), std::move(parameters)};
}
}
