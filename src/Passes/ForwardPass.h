#pragma once

#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/GraphicsGraph.h"

namespace madcat::passes
{
struct Targets { graph::Resource color, depth; };
struct ClearPass
{
    using Inputs = Targets;
    using Outputs = Targets;
    struct Declared { Outputs outputs; };
    struct Parameters { std::array<float,4> color; };
    Declared declare(graph::Builder& builder, const Inputs& input) const
    {
        return {{builder.write(input.color, graph::Use::Color, graph::Write::Overwrite),
            builder.write(input.depth, graph::Use::Depth, graph::Write::Overwrite)}};
    }
    bool execute(graph::PassContext& context, const Declared& declared, const Parameters& parameters) const
    {
        return context.beginRendering(declared.outputs.color, declared.outputs.depth, rhi::AttachmentLoad::Clear, parameters.color) && context.endRendering();
    }
};
struct ForwardPass
{
    struct Inputs
    {
        Targets targets;
        graph::Resource vertices, indices, lighting;
        std::vector<graph::Resource> textures;
    };
    using Outputs = Targets;
    struct Declared { Inputs inputs; Outputs outputs; };
    // Own the frame's parameter bytes; the compiled graph never captures them.
    struct Parameters { std::vector<rhi::IndexedDraw> draws; };
    Declared declare(graph::Builder& builder, const Inputs& input) const
    {
        builder.read(input.vertices, graph::Use::Vertex);
        builder.read(input.indices, graph::Use::Index);
        if (input.lighting.graph) { builder.read(input.lighting, graph::Use::Uniform); }
        for (const auto& texture : input.textures) { builder.read(texture, graph::Use::Sampled); }
        return {input, {builder.write(input.targets.color, graph::Use::Color, graph::Write::Preserve),
            builder.write(input.targets.depth, graph::Use::Depth, graph::Write::Preserve)}};
    }
    bool execute(graph::PassContext& context, const Declared& declared, const Parameters& parameters) const
    {
        if (!context.beginRendering(declared.outputs.color, declared.outputs.depth, rhi::AttachmentLoad::Load, {})) { return false; }
        for (const auto& draw : parameters.draws)
        {
            if (!context.drawIndexed(declared.inputs.vertices, declared.inputs.indices, declared.inputs.lighting, declared.inputs.textures, draw)) { return false; }
        }
        return context.endRendering();
    }
};
struct PresentPass
{
    struct Inputs { graph::Resource color; };
    using Outputs = Inputs;
    struct Declared { Outputs outputs; };
    struct Parameters {};
    Declared declare(graph::Builder& builder, const Inputs& input) const
    { builder.read(input.color, graph::Use::Present); return {input}; }
    // This node exports the graph's produced color. The graph transitions it;
    // only the renderer submits and performs the actual presentation operation.
    bool execute(graph::PassContext& context, const Declared& declared, const Parameters&) const
    { return context.require(declared.outputs.color, graph::Use::Present); }
};

// C++ composition policy selected by the caller. The backend executes the
// returned plan and never depends on concrete Pass implementations.
inline std::expected<graph::GraphicsGraph,std::string> buildForwardGraph(graph::Graph& graph, const graph::GraphicsImports& imports)
{
    const auto clear=graph.addPass("Clear",ClearPass{},Targets{imports.color,imports.depth});
    auto targets=clear.outputs;
    uint32_t forwardNode=UINT32_MAX;
    if (imports.vertices.graph)
    {
        const auto forward=graph.addPass("Forward",ForwardPass{},ForwardPass::Inputs{targets,imports.vertices,imports.indices,imports.lighting,imports.textures});
        forwardNode=forward.node; targets=forward.outputs;
    }
    const auto present=graph.addPass("Present",PresentPass{},PresentPass::Inputs{targets.color});
    auto plan=graph.compile();
    if (!plan) { return std::unexpected(plan.error()); }
    const auto count=plan->nodes().size();
    auto parameters=[clearNode=clear.node,forwardNode,presentNode=present.node,count](const std::array<float,4>& color,std::span<const rhi::IndexedDraw> draws) {
        std::vector<std::any> values(count);
        values[clearNode]=ClearPass::Parameters{color};
        if (forwardNode!=UINT32_MAX) { values[forwardNode]=ForwardPass::Parameters{{draws.begin(),draws.end()}}; }
        values[presentNode]=PresentPass::Parameters{};
        return values;
    };
    return graph::GraphicsGraph{std::move(*plan),std::move(parameters)};
}
}
