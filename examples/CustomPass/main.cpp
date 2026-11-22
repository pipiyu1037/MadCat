#include "Runtime/Application.h"
#include "DisplayGraph.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <limits>
#include <glm/gtc/quaternion.hpp>

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    using namespace madcat;
    try
    {
        auto path=std::filesystem::path(u8"" MADCAT_EXAMPLE_ASSETS)/"models/Suzanne/Suzanne.gltf";
        bool fit=false,hasPath=false;
        for (int arg=1;arg<argc;++arg) {
            const std::filesystem::path value(argv[arg]);
            if (value=="--fit" && !fit) { fit=true; }
            else if (!hasPath && value!="--fit") { path=value; hasPath=true; }
            else { throw std::runtime_error("Usage: CustomPass [--fit] [model.gltf|model.glb]"); }
        }
        auto imported=assets::importGltfGeometry(path);
        if (!imported) { throw std::runtime_error(imported.error().message); }
        scene::LocalTransform initialPose; initialPose.scale=glm::vec3(.7f);
        if (fit)
        {
            glm::dvec3 minimum(std::numeric_limits<double>::max()), maximum(-std::numeric_limits<double>::max());
            auto nodes=imported->sceneRoots;
            while (!nodes.empty()) {
                const auto node=nodes.back(); nodes.pop_back();
                const auto& source=imported->nodes[node];
                nodes.insert(nodes.end(),source.children.begin(),source.children.end());
                if (source.mesh) { for (const auto& primitive : imported->meshes[*source.mesh].primitives) {
                    for (const auto& position : primitive.positions) {
                        const auto point=glm::dvec3(glm::dmat4(source.worldTransform)*glm::dvec4(position,1));
                        minimum=glm::min(minimum,point); maximum=glm::max(maximum,point);
                    }
                } }
            }
            const auto half=(maximum-minimum)*.5;
            const double halfExtent=std::max({half.x,half.y,half.z});
            if (!std::isfinite(halfExtent) || halfExtent<=0) { throw std::runtime_error("Cannot frame empty or degenerate model"); }
            initialPose.scale=glm::vec3(static_cast<float>(.7/halfExtent));
            initialPose.translation=glm::vec3(-(minimum+maximum)*.5*(.7/halfExtent));
        }
        assets::ModelAssets assets;
        const auto model=assets.add(std::move(*imported));
        scene::Scene scene;
        const auto instance=scene::instantiateModel(scene,assets,model);
        if (!instance) { throw std::runtime_error(instance.error()); }
        const auto camera=scene.create(), light=scene.create();
        scene::LocalTransform modelPose=initialPose, cameraPose;
        cameraPose.translation.z=3;
        if (!scene.setLocalTransform(instance->root,modelPose) || !scene.add<scene::PerspectiveCamera>(camera) ||
            !scene.setLocalTransform(camera,cameraPose)) { throw std::runtime_error("Scene setup failed"); }
        auto* sunlight=scene.add<scene::DirectionalLight>(light);
        if (!sunlight) { throw std::runtime_error("Light setup failed"); }
        sunlight->color={1,.8f,.6f}; sunlight->irradiance=4*3.14159265f;

        experiment::ToneMapPass::Parameters displayParameters;
        runtime::ApplicationOptions application;
        application.title="MadCat CustomPass";
        application.forwardColorFormat=rhi::ColorFormat::Rgba16Float;
        application.prepareDepthView=true;
        application.prepareShadow=true;
        const auto shaderRoot=std::filesystem::path(u8"" MADCAT_EXAMPLE_SHADERS);
        experiment::DisplaySettings requestedGraph,activeGraph,lastPresentedGraph;
        application.graphFactory=experiment::makeDisplayGraph(displayParameters,shaderRoot,activeGraph);
        uint64_t graphRequestId=0;
        std::optional<runtime::GraphRequest> pendingGraph;
        std::string graphDiagnostic;
        auto requestGraph=[&](experiment::DisplaySettings settings) {
            requestedGraph=settings;
            pendingGraph=runtime::GraphRequest{++graphRequestId,
                experiment::makeDisplayGraph(displayParameters,shaderRoot,settings)};
        };
        int mode=0,logged=-1;
        struct Controls
        {
            float exposure=0;
            std::array<float,3> color{1,.8f,.6f};
            float irradiance=4*3.14159265f;
            bool operator==(const Controls&) const = default;
        };
        Controls controls, lastPresented;
        bool controlsPresented=false;
        std::string diagnostic;
        auto request=[&](const Controls& draft) {
            if (!std::isfinite(draft.exposure) || draft.exposure < -8 || draft.exposure > 8 ||
                !std::isfinite(draft.irradiance) || draft.irradiance < 0 || draft.irradiance > 32 ||
                !std::ranges::all_of(draft.color,[](float c){return std::isfinite(c) && c>=0 && c<=1;}))
            { diagnostic="Invalid parameters; keeping previous values."; return; }
            controls=draft; diagnostic.clear();
            displayParameters.exposureEV=controls.exposure;
            auto& lightParameters=*scene.tryGet<scene::DirectionalLight>(light);
            lightParameters.color={controls.color[0],controls.color[1],controls.color[2]};
            lightParameters.irradiance=controls.irradiance;
        };
        auto setPose=[&](int preset) {
            modelPose=initialPose;
            modelPose.translation.x+=preset==4 ? .45f : 0;
            auto cameraTransform=cameraPose;
            cameraTransform.translation.x+=preset==6 ? .4f : 0;
            scene::LocalTransform lightPose;
            if (preset==5) { lightPose.rotation=glm::angleAxis(glm::radians(35.f),glm::vec3(0,1,0)); }
            if (!scene.setLocalTransform(instance->root,modelPose) || !scene.setLocalTransform(camera,cameraTransform) ||
                !scene.setLocalTransform(light,lightPose)) { throw std::runtime_error("Scene edit failed"); }
        };
        runtime::SceneCallbacks callbacks;
        callbacks.graphRequest=[&] { return std::exchange(pendingGraph,std::nullopt); };
        callbacks.graphUpdated=[&](const runtime::GraphUpdate& update) {
            if (update.applied) { activeGraph=requestedGraph; }
            graphDiagnostic=update.diagnostic;
        };
        callbacks.ui=[&] {
            auto draft=controls;
            ImGui::SetNextWindowPos({12,12},ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({340,270},ImGuiCond_Always);
            ImGui::Begin("CustomPass Parameters",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::PushItemWidth(185);
            bool edited=ImGui::SliderFloat("Exposure (EV)",&draft.exposure,-8,8,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            edited|=ImGui::ColorEdit3("Light color",draft.color.data(),ImGuiColorEditFlags_Float|ImGuiColorEditFlags_NoPicker|ImGuiColorEditFlags_NoOptions);
            edited|=ImGui::SliderFloat("Irradiance",&draft.irradiance,0,32,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("Restore defaults"))
            {
                draft=Controls{}; edited=true; mode=0;
                setPose(0);
                if (requestedGraph!=experiment::DisplaySettings{} || activeGraph!=experiment::DisplaySettings{}) { requestGraph({}); }
            }
            ImGui::SameLine();
            auto graphSettings=requestedGraph;
            if (ImGui::Checkbox("Forward",&graphSettings.forwardEnabled)) { requestGraph(graphSettings); }
            ImGui::TextUnformatted("Ctrl+click a slider to type a value.");
            ImGui::Text("Active graph: Forward %s",activeGraph.forwardEnabled ? "on" : "off");
            int view=static_cast<int>(graphSettings.view);
            if (ImGui::Combo("View",&view,"Final image\0Camera depth\0HDR range\0Shadow map\0")) {
                graphSettings.view=static_cast<experiment::DisplayView>(view); requestGraph(graphSettings);
            }
            ImGui::Text("Active view: %s",experiment::displayViewName(activeGraph.view));
            if (activeGraph.view==experiment::DisplayView::Depth) { ImGui::TextUnformatted("1 - device depth (nonlinear)"); }
            if (activeGraph.view==experiment::DisplayView::HdrRange) {
                ImGui::TextUnformatted("Magenta: any RGB > 1; else RGB.");
                ImGui::TextUnformatted("Before exposure and tone mapping.");
            }
            if (activeGraph.view==experiment::DisplayView::Shadow) {
                ImGui::TextUnformatted("1 - light depth (orthographic)");
                ImGui::TextUnformatted("1024 x 1024; independent of Forward.");
            }
            if (!graphDiagnostic.empty()) { ImGui::TextWrapped("%s",graphDiagnostic.c_str()); }
            if (!diagnostic.empty()) { ImGui::TextWrapped("%s",diagnostic.c_str()); }
            ImGui::PopItemWidth(); ImGui::End();
            if (edited) { request(draft); }
        };
        callbacks.keyPressed=[&](char key) {
            if (key<'1' || key>'7') { return; }
            mode=key-'1';
            Controls draft;
            draft.exposure=mode==1 ? -2.f : mode==2 ? 2.f : 0.f;
            draft.irradiance=mode==3 ? 0.f : 4*3.14159265f;
            request(draft);
            setPose(mode);
        };
        callbacks.presented=[&] {
            if (activeGraph.forwardEnabled!=lastPresentedGraph.forwardEnabled) {
                std::fprintf(stderr,"CustomPass: Forward %s presented.\n",activeGraph.forwardEnabled ? "on" : "off");
            }
            if (activeGraph.view!=lastPresentedGraph.view) {
                std::fprintf(stderr,"CustomPass: view %s presented.\n",experiment::displayViewName(activeGraph.view));
            }
            lastPresentedGraph=activeGraph;
            if (logged!=mode) { std::fprintf(stderr,"CustomPass: mode %d presented.\n",mode); logged=mode; }
            if (!controlsPresented || controls!=lastPresented)
            {
                std::fprintf(stderr,"CustomPass: controls exposure %.4f, light (%.4f,%.4f,%.4f) x %.4f presented.\n",
                    controls.exposure,controls.color[0],controls.color[1],controls.color[2],controls.irradiance);
                lastPresented=controls; controlsPresented=true;
            }
        };
        return runtime::run(application,scene,assets,camera,callbacks);
    }
    catch (const std::exception& error) { std::fprintf(stderr,"CustomPass failed: %s\n",error.what()); }
    return EXIT_FAILURE;
}
