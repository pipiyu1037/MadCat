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
};
struct GraphicsGraph
{
    Plan plan;
    std::function<std::vector<std::any>(const std::array<float,4>&,std::span<const rhi::IndexedDraw>)> parameters;
};
using GraphicsGraphFactory = std::function<std::expected<GraphicsGraph,std::string>(Graph&,const GraphicsImports&)>;
}
