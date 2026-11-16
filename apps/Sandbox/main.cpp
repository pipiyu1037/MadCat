#include "Runtime/Application.h"
#include "TriangleScene.h"
#include "ModelScene.h"
#include "Passes/ForwardPass.h"
#include "Passes/DepthViewPass.h"
#include "Passes/ShadowPass.h"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    madcat::assets::ModelAssets assets;
    std::unique_ptr<madcat::sandbox::ModelScene> modelScene;
    std::optional<madcat::render::GeometryBatch> model;
    bool depthView = false, shadowView = false, shadows = false;
    try
    {
        std::optional<std::filesystem::path> modelPath;
        for (int argument=1; argument<argc; ++argument)
        {
            const std::filesystem::path option(argv[argument]);
            if (option=="--depth-view" && !depthView) { depthView=true; }
            else if (option=="--shadow-view" && !shadowView) { shadowView=true; }
            else if (option=="--shadows" && !shadows) { shadows=true; }
            else if (option=="--gltf" && !modelPath && argument+1<argc) { modelPath=argv[++argument]; }
            else
            {
                std::fprintf(stderr, "Usage: MadCat [--gltf <model.gltf|model.glb>] [--depth-view | --shadow-view | --shadows]\n");
                return EXIT_FAILURE;
            }
        }
        if ((shadowView || shadows) && (!modelPath || (depthView+shadowView+shadows)>1))
        { throw std::runtime_error("Shadow modes require --gltf and cannot be combined with another mode"); }
        if (modelPath)
        {
            auto imported = madcat::assets::importGltfGeometry(*modelPath);
            if (!imported)
            {
                const auto filename = imported.error().file.u8string();
                std::fprintf(stderr, "MadCat: glTF import failed: %s: %s\n",
                    reinterpret_cast<const char*>(filename.c_str()), imported.error().message.c_str());
                return EXIT_FAILURE;
            }
            const auto id = assets.add(std::move(*imported));
            modelScene = std::make_unique<madcat::sandbox::ModelScene>(assets, id);
            auto draws = modelScene->draws(1280.f / 720.f);
            if (!draws) { throw std::runtime_error(draws.error()); }
            auto packed = madcat::render::prepareGeometry(assets, *draws);
            if (!packed) { throw std::runtime_error(packed.error()); }
            model = std::move(*packed);
            std::fprintf(stderr, "MadCat: shared model geometry ready; %zu primitives, %zu vertices, %zu indices; %zu initial draws.\n",
                model->ranges.size(), model->vertices.size(), model->indices.size(), draws->size());
            std::fprintf(stderr, "MadCat: model keys 1 default, 2 move first, 3 camera, 4 delete first, 5 delete second, 6 restore, 7 reverse draws, 8 move second.\n");
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "MadCat: model preparation failed: %s\n", error.what());
        return EXIT_FAILURE;
    }

    try
    {
        madcat::render::GeometryBatch triangle;
        triangle.vertices = {
            {{0.0f,-0.2f,0},{1.0f/3,1.0f/3,1.0f/3}},
            {{0.6f,-0.6f,0},{0,1,0}}, {{0.0f,0.6f,0},{1,0,0}}, {{-0.6f,-0.6f,0},{0,0,1}}
        };
        triangle.indices = {2,3,0,3,1,0,1,2,0};
        const auto& geometry = model ? *model : triangle;
        madcat::sandbox::TriangleScene triangleScene;
        int requestedPose = 0, activePose = 0, loggedPose = -1;
        int requestedLighting = 0, activeLighting = 0, loggedLighting = -1;
        int shadowMode = 2, loggedShadowMode = -1, loggedView = -1;
        size_t drawCount = 0;
        auto requestedView = shadows ? madcat::rhi::GeometryView::Lit :
            modelScene ? madcat::rhi::GeometryView::Material : madcat::rhi::GeometryView::Color;

        madcat::runtime::ApplicationOptions options;
        options.prepareDepthView = depthView || shadowView;
        options.prepareShadow = shadowView || shadows;
        options.graphFactory = shadowView ? madcat::passes::buildShadowViewGraph : shadows ? madcat::passes::buildShadowedForwardGraph :
            depthView ? madcat::passes::buildDepthViewGraph : madcat::passes::buildForwardGraph;
        madcat::runtime::GraphicsCallbacks callbacks;
        callbacks.keyPressed = [&](char key) {
            if (key >= '1' && key <= (modelScene ? '9' : '7')) { requestedPose = key-'1'; }
            if (key == '0' && modelScene) { requestedPose = 9; }
            if (key == 'C') { requestedView = madcat::rhi::GeometryView::Color; }
            if (key == 'N') { requestedView = madcat::rhi::GeometryView::WorldNormal; }
            if (key == 'U') { requestedView = madcat::rhi::GeometryView::Texcoord; }
            if (key == 'M') { requestedView = madcat::rhi::GeometryView::Material; }
            if (key == 'L' && modelScene) { requestedView = madcat::rhi::GeometryView::Lit; }
            if (key == 'J' && modelScene) { requestedLighting = (requestedLighting+1)%3; }
            if (key == 'K' && shadows) { shadowMode = (shadowMode+1)%3; }
        };
        callbacks.frame = [&](const madcat::runtime::FrameInfo& info) -> std::expected<madcat::runtime::RenderFrame,std::string> {
            if (activePose != requestedPose)
            {
                if (modelScene ? !modelScene->selectPose(requestedPose) :
                    !triangleScene.selectPose(static_cast<madcat::sandbox::TrianglePose>(requestedPose)))
                { return std::unexpected("Scene pose selection failed"); }
                activePose = requestedPose;
            }
            madcat::runtime::RenderFrame frame;
            if (modelScene)
            {
                if (activeLighting != requestedLighting)
                {
                    if (!modelScene->selectLighting(requestedLighting)) { return std::unexpected("Light preset failed"); }
                    activeLighting = requestedLighting;
                }
                auto snapshot = shadowView ? modelScene->shadowDraws() : modelScene->draws(info.aspect);
                if (!snapshot) { return std::unexpected(snapshot.error()); }
                auto draws = madcat::render::resolveDraws(geometry,*snapshot);
                if (!draws) { return std::unexpected(draws.error()); }
                frame.draws = std::move(*draws);
                const auto light = modelScene->lighting(shadows ? info.aspect : 0,shadowMode);
                if (!light) { return std::unexpected(light.error()); }
                frame.lighting = *light;
                if (shadows)
                {
                    auto shadowSnapshot = modelScene->shadowDraws();
                    if (!shadowSnapshot) { return std::unexpected(shadowSnapshot.error()); }
                    auto resolved = madcat::render::resolveDraws(geometry,*shadowSnapshot);
                    if (!resolved) { return std::unexpected(resolved.error()); }
                    frame.shadowDraws = std::move(*resolved);
                }
            }
            else
            {
                const auto transforms = triangleScene.drawTransforms(info.aspect);
                if (!transforms) { return std::unexpected("Scene camera/model matrix extraction failed"); }
                for (size_t index=0; index<transforms->count; ++index)
                { frame.draws.push_back({transforms->transforms[index],0,static_cast<uint32_t>(geometry.indices.size()),0}); }
            }
            for (auto& draw : frame.draws) { draw.transform.view = requestedView; }
            drawCount = frame.draws.size();
            return frame;
        };
        callbacks.presented = [&] {
            if (modelScene)
            {
                if (shadows && loggedShadowMode != shadowMode) {
                    loggedShadowMode = shadowMode;
                    std::fprintf(stderr,"MadCat: shadows presented; %d (K=off/hard/PCF).\n",shadowMode);
                }
                if (loggedLighting != activeLighting) {
                    loggedLighting = activeLighting;
                    std::fprintf(stderr,"MadCat: lighting presented; %d (J=default/rotated/off).\n",loggedLighting);
                }
                if (loggedPose != activePose) {
                    std::fprintf(stderr,"MadCat: model instances presented; pose %d, %zu draws, %zu shared primitives.\n",
                        activePose+1,drawCount,geometry.ranges.size());
                }
            }
            if (loggedPose != activePose) {
                std::fprintf(stderr,"MadCat: Scene pose presented; %d (keys 1-%d).\n",activePose+1,modelScene ? 9 : 7);
                loggedPose = activePose;
            }
            if (loggedView != static_cast<int>(requestedView)) {
                loggedView = static_cast<int>(requestedView);
                std::fprintf(stderr,"MadCat: geometry view presented; %d (L=lit, M=material, C=color, N=world normal, U=UV; magenta=unavailable).\n",loggedView);
            }
        };
        return madcat::runtime::runGraphics(options,assets,geometry,callbacks);
    }
    catch (const std::exception& error) { std::fprintf(stderr,"MadCat: experiment failed: %s\n",error.what()); }
    return EXIT_FAILURE;
}
