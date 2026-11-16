#pragma once
#include "DepthViewPass.h"

namespace madcat::passes
{
struct ShadowPass
{
    struct Inputs { graph::Resource depth, vertices, indices; std::vector<graph::Resource> textures; };
    struct Outputs { graph::Resource depth; };
    struct Declared { Inputs inputs; Outputs outputs; };
    struct Parameters { std::vector<rhi::IndexedDraw> draws; };
    Declared declare(graph::Builder& builder, const Inputs& input) const
    {
        builder.read(input.vertices, graph::Use::Vertex);
        builder.read(input.indices, graph::Use::Index);
        for (const auto texture : input.textures) { builder.read(texture, graph::Use::Sampled); }
        return {input, {builder.write(input.depth, graph::Use::Depth, graph::Write::Overwrite)}};
    }
    bool execute(graph::PassContext& context, const Declared& declared, const Parameters& parameters) const
    {
        if (!context.beginRendering({}, declared.outputs.depth, rhi::AttachmentLoad::Clear, {})) { return false; }
        for (const auto& draw : parameters.draws)
        {
            if (!context.drawIndexed(declared.inputs.vertices, declared.inputs.indices, {}, declared.inputs.textures, draw)) { return false; }
        }
        return context.endRendering();
    }
};
inline std::expected<graph::GraphicsGraph,std::string> buildShadowViewGraph(graph::Graph& graph, const graph::GraphicsImports& imports)
{
    const auto depth = graph.createDepthTexture("DirectionalShadow", {1024,1024});
    const auto shadow = graph.addPass("Shadow", ShadowPass{}, ShadowPass::Inputs{depth,imports.vertices,imports.indices,imports.textures});
    const auto display = graph.addPass("DepthView", DepthViewPass{}, DepthViewPass::Inputs{imports.color,shadow.outputs.depth});
    const auto present = graph.addPass("Present", PresentPass{}, PresentPass::Inputs{display.outputs.color});
    auto plan = graph.compile();
    if (!plan) { return std::unexpected(plan.error()); }
    auto parameters = [shadowNode=shadow.node, displayNode=display.node, presentNode=present.node, count=plan->nodes().size()]
        (const std::array<float,4>&, std::span<const rhi::IndexedDraw> draws, std::span<const rhi::IndexedDraw>) {
        std::vector<std::any> values(count);
        values[shadowNode] = ShadowPass::Parameters{{draws.begin(),draws.end()}};
        values[displayNode] = DepthViewPass::Parameters{};
        values[presentNode] = PresentPass::Parameters{};
        return values;
    };
    return graph::GraphicsGraph{std::move(*plan),std::move(parameters)};
}

inline std::expected<graph::GraphicsGraph,std::string> buildShadowedForwardGraph(graph::Graph& graph, const graph::GraphicsImports& imports)
{
    const auto depth=graph.createDepthTexture("DirectionalShadow",{1024,1024});
    const auto shadow=graph.addPass("Shadow",ShadowPass{},ShadowPass::Inputs{depth,imports.vertices,imports.indices,imports.textures});
    const auto clear=graph.addPass("Clear",ClearPass{},Targets{imports.color,imports.depth});
    const auto forward=graph.addPass("Forward",ForwardPass{},ForwardPass::Inputs{
        clear.outputs,imports.vertices,imports.indices,imports.lighting,imports.textures,shadow.outputs.depth});
    const auto present=graph.addPass("Present",PresentPass{},PresentPass::Inputs{forward.outputs.color});
    auto plan=graph.compile();
    if (!plan) { return std::unexpected(plan.error()); }
    auto parameters=[shadowNode=shadow.node,clearNode=clear.node,forwardNode=forward.node,presentNode=present.node,count=plan->nodes().size()]
        (const std::array<float,4>& color,std::span<const rhi::IndexedDraw> draws,std::span<const rhi::IndexedDraw> shadowDraws) {
        std::vector<std::any> values(count);
        values[shadowNode]=ShadowPass::Parameters{{shadowDraws.begin(),shadowDraws.end()}};
        values[clearNode]=ClearPass::Parameters{color};
        values[forwardNode]=ForwardPass::Parameters{{draws.begin(),draws.end()}};
        values[presentNode]=PresentPass::Parameters{};
        return values;
    };
    return graph::GraphicsGraph{std::move(*plan),std::move(parameters)};
}
}
