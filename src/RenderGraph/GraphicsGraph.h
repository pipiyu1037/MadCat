#pragma once

#include "RenderGraph.h"

namespace madcat::graph
{
// Imported graphics resources supplied by the presenter. Construction policy
// comes from the caller; this contract contains no native GPU handles.
struct GraphicsImports
{
    Resource color, depth, vertices, indices, lighting;
    std::vector<Resource> textures;
    uint32_t width = 0, height = 0;
    rhi::GraphicsPipelineFactory prepareGraphics;
    bool presentationSrgb = false;
};
struct GraphicsGraph
{
    Plan plan;
    // Camera and light draw lists are separate value snapshots, fixed by record.
    std::function<std::vector<std::any>(const std::array<float,4>&,std::span<const rhi::IndexedDraw>,std::span<const rhi::IndexedDraw>)> parameters;
};
using GraphicsGraphFactory = std::function<std::expected<GraphicsGraph,std::string>(Graph&,const GraphicsImports&)>;
}
