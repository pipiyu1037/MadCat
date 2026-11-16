#include "Runtime/Application.h"
#include "Passes/ForwardPass.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    try
    {
        // 1. Load a CPU asset and instantiate its nodes as Scene entities.
        if (argc > 2) { throw std::runtime_error("Usage: BasicScene [model.gltf|model.glb]"); }
        const auto path = argc == 2 ? std::filesystem::path(argv[1]) :
            std::filesystem::path(u8"" MADCAT_EXAMPLE_ASSETS)/"models/Suzanne/Suzanne.gltf";
        auto imported = madcat::assets::importGltfGeometry(path);
        if (!imported) { throw std::runtime_error(imported.error().message); }
        madcat::assets::ModelAssets assets;
        const auto model = assets.add(std::move(*imported));
        madcat::scene::Scene scene;
        const auto instance = madcat::scene::instantiateModel(scene,assets,model);
        if (!instance) { throw std::runtime_error(instance.error()); }

        // 2. Set the model pose, then explicitly create the active camera/light.
        madcat::scene::LocalTransform modelPose;
        modelPose.scale = glm::vec3(.7f);
        const auto camera = scene.create();
        const auto light = scene.create();
        madcat::scene::LocalTransform cameraPose;
        cameraPose.translation.z = 3;
        if (!scene.setLocalTransform(instance->root,modelPose) ||
            !scene.add<madcat::scene::PerspectiveCamera>(camera) ||
            !scene.setLocalTransform(camera,cameraPose))
        { throw std::runtime_error("Model/camera setup failed"); }
        auto* sunlight = scene.add<madcat::scene::DirectionalLight>(light);
        if (!sunlight) { throw std::runtime_error("Light setup failed"); }
        sunlight->color = {1,.8f,.6f};

        // 3. The experiment chooses the C++ rendering graph. The existing
        // factory connects Clear -> Forward -> Present, including output.
        madcat::runtime::ApplicationOptions application;
        application.title = "MadCat BasicScene";
        application.graphFactory = madcat::passes::buildForwardGraph;

        // 4. Optional user updates: keys 1/2/3/4 restore, move the model,
        // move the camera, or turn off the light. Edits precede extraction.
        int pose = 0, applied = -1, logged = -1;
        madcat::runtime::SceneCallbacks callbacks;
        callbacks.keyPressed = [&](char key) {
            if (key >= '1' && key <= '4') { pose = key-'1'; }
        };
        callbacks.update = [&](const madcat::runtime::FrameInfo&) {
            if (pose == applied) { return; }
            modelPose.translation.x = pose == 1 ? .45f : 0.f;
            cameraPose.translation.x = pose == 2 ? .45f : 0.f;
            if (!scene.setLocalTransform(instance->root,modelPose) || !scene.setLocalTransform(camera,cameraPose))
            { throw std::runtime_error("Scene edit failed"); }
            scene.tryGet<madcat::scene::DirectionalLight>(light)->irradiance = pose == 3 ? 0.f : 3.14159265f;
            applied = pose;
        };
        callbacks.presented = [&] {
            if (logged != pose) { std::fprintf(stderr,"BasicScene: pose %d presented.\n",pose); logged = pose; }
        };

        // Blocking engine entry: world update, RenderScene extraction, GPU
        // preparation, window loop and retirement are shared engine work.
        return madcat::runtime::run(application,scene,assets,camera,callbacks);
    }
    catch (const std::exception& error) { std::fprintf(stderr,"BasicScene failed: %s\n",error.what()); }
    return EXIT_FAILURE;
}
