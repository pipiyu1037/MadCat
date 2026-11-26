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
    rhi::ComputePipelineFactory prepareCompute;
    bool presentationSrgb = false;
    Resource uiFont; // initialized, immutable atlas when a UI callback is enabled
    bool storageGeometry = false;
};
struct GraphicsGraph
{
    Plan plan;
    // Camera and light draw lists are separate value snapshots, fixed by record.
    std::function<std::vector<std::any>(const std::array<float,4>&,std::span<const rhi::IndexedDraw>,std::span<const rhi::IndexedDraw>)> parameters;
    // Optional synchronous completion callback. Views expire on return. Called
    // after the submission fence, including safe graph replacement and shutdown.
    // False/exception stops the application; no exception crosses destruction.
    std::function<bool(std::span<const rhi::BufferReadback>)> completed;
};
using GraphicsGraphFactory = std::function<std::expected<GraphicsGraph,std::string>(Graph&,const GraphicsImports&)>;
}
