#pragma once

#include "RenderScene/RenderScene.h"
#include "RenderGraph/GraphicsGraph.h"
#include <filesystem>

namespace madcat::runtime
{
// Fixed forward Shader ABI for this increment. The graph is supplied by the
// experiment; these flags only prepare the existing optional backend pipelines.
struct ApplicationOptions
{
    std::string title = "MadCat";
    uint32_t width = 1280, height = 720;
    graph::GraphicsGraphFactory graphFactory;
    std::filesystem::path shaderDirectory; // empty: configured build/shaders/triangle
    bool prepareDepthView = false, prepareShadow = false;
    rhi::ColorFormat forwardColorFormat = rhi::ColorFormat::Presentation;
};
struct FrameInfo { float aspect; double deltaSeconds; };
struct RenderFrame
{
    std::array<float,4> clearColor{.04f,.08f,.16f,1};
    std::vector<rhi::IndexedDraw> draws, shadowDraws;
    rhi::FrameLighting lighting;
};
struct GraphicsCallbacks
{
    std::function<std::expected<RenderFrame,std::string>(const FrameInfo&)> frame;
    std::function<void(char)> keyPressed; // uppercase ASCII letters and digits
    std::function<void()> presented; // present accepted; GPU/WSI completion can still be pending
};
// Advanced frame-data entry used by Sandbox. All arguments/captures must live
// through this blocking call. Startup geometry/materials are immutable; frame
// values own their bytes. Returns only after dependent GPU/WSI work retires.
// Callback exceptions stop rendering and return failure; none cross C callbacks.
// Single-threaded, one application at a time; 0 = clean exit, nonzero = failure.
[[nodiscard]] int runGraphics(const ApplicationOptions& options, const assets::ModelAssets& assets,
    const render::GeometryBatch& geometry, const GraphicsCallbacks& callbacks);

struct SceneCallbacks
{
    std::function<void(const FrameInfo&)> update;
    std::function<void(char)> keyPressed;
    std::function<void()> presented; // same completion distinction as GraphicsCallbacks
};
// Scene entry: user owns Scene/Assets/camera and algorithm composition. Updates
// run before world propagation and extraction, outside Scene queries. The engine
// packs/uploads startup geometry once and resolves fresh per-frame snapshots.
// Transform/light edits and instance deletion/restoration using startup geometry
// are supported; new geometry/materials require restarting this run.
// This first Scene entry supplies unshadowed forward draws. Advanced shadow
// snapshots are provided explicitly through runGraphics by existing Sandbox.
[[nodiscard]] int run(const ApplicationOptions& options, scene::Scene& scene,
    const assets::ModelAssets& assets, ecs::Entity camera, const SceneCallbacks& callbacks = {});
}
