#include "Application.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>

namespace madcat::runtime
{
int run(const ApplicationOptions& options, scene::Scene& scene, const assets::ModelAssets& assets,
    ecs::Entity camera, const SceneCallbacks& callbacks)
{
    try
    {
        if (!options.width || !options.height || options.prepareShadow)
        { throw std::runtime_error("Scene application requires a nonzero extent and unshadowed frame input"); }
        if (!scene.updateWorldTransforms()) { throw std::runtime_error("Startup world transform update failed"); }
        auto snapshot = render::extractDraws(scene, assets, camera, float(options.width)/float(options.height));
        if (!snapshot) { throw std::runtime_error(snapshot.error()); }
        auto geometry = render::prepareGeometry(assets, *snapshot);
        if (!geometry) { throw std::runtime_error(geometry.error()); }
        GraphicsCallbacks graphics;
        graphics.keyPressed = callbacks.keyPressed;
        graphics.presented = callbacks.presented;
        graphics.ui = callbacks.ui;
        graphics.frame = [&](const FrameInfo& info) -> std::expected<RenderFrame,std::string> {
            if (callbacks.update) { callbacks.update(info); }
            if (!scene.updateWorldTransforms()) { return std::unexpected("Frame world transform update failed"); }
            auto draws = render::extractDraws(scene, assets, camera, info.aspect);
            if (!draws) { return std::unexpected(draws.error()); }
            auto resolved = render::resolveDraws(*geometry, *draws);
            if (!resolved) { return std::unexpected(resolved.error()); }
            auto lighting = render::extractLighting(scene);
            if (!lighting) { return std::unexpected(lighting.error()); }
            RenderFrame frame;
            frame.draws = std::move(*resolved);
            for (auto& draw : frame.draws) { draw.transform.view = rhi::GeometryView::Lit; }
            frame.lighting = *lighting;
            return frame;
        };
        return runGraphics(options, assets, *geometry, graphics);
    }
    catch (const std::exception& error) { std::fprintf(stderr, "MadCat scene application failed: %s\n", error.what()); }
    catch (...) { std::fprintf(stderr, "MadCat scene application failed: unknown exception.\n"); }
    return EXIT_FAILURE;
}
}
