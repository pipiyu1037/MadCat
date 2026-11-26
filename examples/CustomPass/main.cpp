#include "Runtime/Application.h"
#include "DisplayGraph.h"
#include "PbrSpheres.h"
#include "Shader/GraphicsUpdate.h"
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
        bool fit=false,hasPath=false,pbr=false,spheres=false,cornell=false,rayQuery=false,traceRays=false;
        auto shaderSources=std::filesystem::path(u8"" MADCAT_UPDATE_SOURCES);
        for (int arg=1;arg<argc;++arg) {
            const std::filesystem::path value(argv[arg]);
            if (value=="--fit" && !fit) { fit=true; }
            else if (value=="--pbr") { pbr=true; }
            else if (value=="--pbr-spheres") { spheres=true; pbr=true; }
            else if (value=="--cornell-box") { cornell=true; pbr=true; }
            else if (value=="--ray-query-buffers") { rayQuery=true; }
            else if (value=="--ray-query") { rayQuery=true; traceRays=true; }
            else if (value=="--shader-sources") {
                if (++arg==argc) { throw std::runtime_error("--shader-sources requires a directory"); }
                shaderSources=std::filesystem::path(argv[arg]);
            }
            else if (!hasPath && !value.native().starts_with(std::filesystem::path("--").native())) { path=value; hasPath=true; }
            else { throw std::runtime_error("Usage: CustomPass [--fit] [--pbr | --pbr-spheres | --cornell-box] [--ray-query-buffers | --ray-query] [--shader-sources directory] [model.gltf|model.glb]"); }
        }
        if (cornell) {
            if (spheres || hasPath) { throw std::runtime_error("--cornell-box supplies its own scene"); }
            path=std::filesystem::path(u8"" MADCAT_EXAMPLE_ASSETS)/"models/CornellBox/CornellBox.gltf"; fit=true;
        }
        if (spheres && (hasPath || fit)) { throw std::runtime_error("--pbr-spheres supplies its own scene and framing"); }
        auto imported=spheres ? std::expected<assets::GltfGeometry,assets::GltfError>(experiment::makePbrSpheres()) : assets::importGltfGeometry(path);
        if (!imported) { throw std::runtime_error(imported.error().message); }
        scene::LocalTransform initialPose; initialPose.scale=glm::vec3(.7f);
        if (spheres) { initialPose.scale=glm::vec3(1); }
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
        if (cornell) {
            // Source room opens toward -Z. Face it toward our +Z camera and
            // leave room for the parameter panel without editing the asset.
            initialPose.rotation=glm::angleAxis(glm::radians(180.f),glm::vec3(0,1,0));
            initialPose.translation.x+=.4f;
        }
        assets::ModelAssets assets;
        const auto model=assets.add(std::move(*imported));
        scene::Scene scene;
        const auto instance=scene::instantiateModel(scene,assets,model);
        if (!instance) { throw std::runtime_error(instance.error()); }
        const auto camera=scene.create(), light=scene.create();
        scene::LocalTransform modelPose=initialPose, cameraPose;
        cameraPose.translation.z=3;
        auto* cameraParameters=scene.add<scene::PerspectiveCamera>(camera);
        if (cornell && cameraParameters) { cameraParameters->verticalFov=glm::radians(45.f); }
        if (!scene.setLocalTransform(instance->root,modelPose) || !cameraParameters ||
            !scene.setLocalTransform(camera,cameraPose)) { throw std::runtime_error("Scene setup failed"); }
        auto* sunlight=scene.add<scene::DirectionalLight>(light);
        if (!sunlight) { throw std::runtime_error("Light setup failed"); }
        sunlight->color={1,.8f,.6f}; sunlight->irradiance=4*3.14159265f;

        experiment::ToneMapPass::Parameters displayParameters;
        runtime::ApplicationOptions application;
        application.title="MadCat CustomPass";
        application.forwardColorFormat=rhi::ColorFormat::Rgba16Float;
        application.physicallyBased=pbr;
        application.enableRayQuery=rayQuery;
        if (pbr) { std::fprintf(stderr,"CustomPass: PBR constant factors; roughness floor .05.\n"); }
        application.prepareDepthView=true;
        application.prepareShadow=true;
        std::optional<experiment::GeometryReference> computeReference;
        std::shared_ptr<experiment::RayProbeState> rayProbe;
        if (rayQuery) {
            if (!scene.updateWorldTransforms()) { throw std::runtime_error("Compute reference world update failed"); }
            const auto draws=render::extractDraws(scene,assets,camera,float(application.width)/float(application.height));
            if (!draws) { throw std::runtime_error(draws.error()); }
            auto geometry=render::prepareGeometry(assets,*draws);
            if (!geometry) { throw std::runtime_error(geometry.error()); }
            computeReference=experiment::geometryReference(*geometry);
            if (traceRays) {
                const auto resolved=render::resolveDraws(*geometry,*draws);
                if (!resolved) { throw std::runtime_error(resolved.error()); }
                rayProbe=std::make_shared<experiment::RayProbeState>(std::move(*geometry),*resolved);
            }
        }
        const shader::GraphicsUpdateOptions shaderUpdate{
            std::filesystem::path(u8"" MADCAT_UPDATE_CMAKE),std::filesystem::path(u8"" MADCAT_UPDATE_SCRIPT),
            std::filesystem::path(u8"" MADCAT_UPDATE_OUTPUT),shaderSources,std::filesystem::path(u8"" MADCAT_EXAMPLE_SHADERS)};
        auto startupShaders=shader::prepareGraphicsUpdate(shaderUpdate,false);
        if (!startupShaders) { throw std::runtime_error(startupShaders.error()); }
        auto shaderRoot=std::move(*startupShaders);
        std::fprintf(stderr,"CustomPass: Shader revision 0 captured; %s.\n",shaderRoot.string().c_str());
        experiment::DisplaySettings requestedGraph,activeGraph,lastPresentedGraph;
        application.graphFactory=experiment::makeDisplayGraph(displayParameters,shaderRoot,activeGraph,computeReference,rayProbe);
        uint64_t graphRequestId=0;
        std::optional<runtime::GraphRequest> pendingGraph;
        std::optional<std::filesystem::path> pendingShaders;
        bool compileRequested=false,shaderRejected=false;
        uint64_t shaderRevision=0;
        std::string shaderDiagnostic="Display and HDR shaders captured. Compile + apply explicitly rebuilds both.";
        std::string graphDiagnostic;
        auto requestGraph=[&](experiment::DisplaySettings settings) {
            requestedGraph=settings;
            pendingShaders.reset();
            pendingGraph=runtime::GraphRequest{++graphRequestId,
                experiment::makeDisplayGraph(displayParameters,shaderRoot,settings,computeReference,rayProbe)};
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
            if (preset==7 || preset==8) {
                modelPose.scale *= glm::vec3(preset==8 ? -1.25f : 1.25f,.75f,1.1f);
                modelPose.rotation = modelPose.rotation * glm::angleAxis(glm::radians(25.f),glm::vec3(0,1,0));
            }
            auto cameraTransform=cameraPose;
            cameraTransform.translation.x+=preset==6 ? .4f : 0;
            scene::LocalTransform lightPose;
            if (preset==5) { lightPose.rotation=glm::angleAxis(glm::radians(35.f),glm::vec3(0,1,0)); }
            if (cornell) {
                // Direct illumination from the opening, above and to one side.
                // This is not the source scene's emissive ceiling area light.
                const glm::vec3 direction=glm::normalize(glm::vec3(.4f,.8f,1));
                const auto alignment=glm::normalize(glm::quat(1.f+direction.z,-direction.y,direction.x,0));
                lightPose.rotation=alignment*lightPose.rotation;
            }
            if (!scene.setLocalTransform(instance->root,modelPose) || !scene.setLocalTransform(camera,cameraTransform) ||
                !scene.setLocalTransform(light,lightPose)) { throw std::runtime_error("Scene edit failed"); }
        };
        if (cornell) { setPose(0); }
        runtime::SceneCallbacks callbacks;
        callbacks.graphRequest=[&] {
            if (std::exchange(compileRequested,false))
            {
                auto candidate=shader::prepareGraphicsUpdate(shaderUpdate,true);
                if (!candidate)
                {
                    shaderRejected=true; shaderDiagnostic=candidate.error();
                    std::fprintf(stderr,"CustomPass: Shader update rejected; keeping revision %llu.\n%s\n",
                        static_cast<unsigned long long>(shaderRevision),shaderDiagnostic.c_str());
                }
                else
                {
                    pendingShaders=std::move(*candidate);
                    pendingGraph=runtime::GraphRequest{++graphRequestId,
                        experiment::makeDisplayGraph(displayParameters,*pendingShaders,requestedGraph,computeReference,rayProbe)};
                }
            }
            return std::exchange(pendingGraph,std::nullopt);
        };
        callbacks.graphUpdated=[&](const runtime::GraphUpdate& update) {
            if (update.applied) { activeGraph=requestedGraph; }
            graphDiagnostic=update.diagnostic;
            if (pendingShaders)
            {
                shaderRejected=!update.applied;
                if (update.applied)
                {
                    shaderRoot=std::move(*pendingShaders); ++shaderRevision;
                    shaderDiagnostic="Display and HDR shaders applied. Hover here for the latest result.";
                    std::fprintf(stderr,"CustomPass: Shader revision %llu applied; %s.\n",
                        static_cast<unsigned long long>(shaderRevision),shaderRoot.string().c_str());
                }
                else
                {
                    shaderDiagnostic=update.diagnostic;
                    std::fprintf(stderr,"CustomPass: Shader preparation rejected; keeping revision %llu.\n",
                        static_cast<unsigned long long>(shaderRevision));
                }
                pendingShaders.reset();
            }
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
            ImGui::SameLine();
            if (ImGui::Checkbox("Normal map",&graphSettings.normalMapping)) { requestGraph(graphSettings); }
            ImGui::TextUnformatted("Ctrl+click a slider to type a value.");
            ImGui::Text("Active graph: Forward %s",activeGraph.forwardEnabled ? "on" : "off");
            ImGui::SameLine();
            if (ImGui::SmallButton("Compile + apply")) { compileRequested=true; }
            if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Rebuild display/HDR shaders and apply a complete candidate.\nR is the scene shortcut. Compilation pauses this window."); }
            int view=static_cast<int>(graphSettings.view);
            if (ImGui::Combo("View",&view,"Final image\0Camera depth\0HDR range\0Shadow map\0Base color\0")) {
                graphSettings.view=static_cast<experiment::DisplayView>(view); requestGraph(graphSettings);
            }
            ImGui::Text("Active view: %s",experiment::displayViewName(activeGraph.view));
            ImGui::SameLine();
            if (shaderRejected) { ImGui::TextColored({1,.4f,.3f,1},"Update failed"); }
            else { ImGui::Text("Shader %llu",static_cast<unsigned long long>(shaderRevision)); }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip(); ImGui::PushTextWrapPos(550);
                ImGui::TextUnformatted(shaderDiagnostic.c_str());
                ImGui::PopTextWrapPos(); ImGui::EndTooltip();
            }
            if (activeGraph.view==experiment::DisplayView::Depth) { ImGui::TextUnformatted("1 - device depth (nonlinear)"); }
            if (activeGraph.view==experiment::DisplayView::HdrRange) {
                ImGui::TextUnformatted("Magenta: any RGB > 1; else RGB.");
                ImGui::TextUnformatted("Cyan: NaN/Inf; before exposure.");
            }
            if (activeGraph.view==experiment::DisplayView::Shadow) {
                ImGui::TextUnformatted("1 - light depth (orthographic)");
                ImGui::TextUnformatted("1024 x 1024; independent of Forward.");
            }
            if (spheres && activeGraph.view==experiment::DisplayView::Final) {
                ImGui::TextUnformatted("Top: dielectric; bottom: metal.");
                ImGui::TextUnformatted("Roughness L->R: 0, .2, .4, .7, 1.");
            }
            if (activeGraph.view==experiment::DisplayView::BaseColor) { ImGui::TextUnformatted("Unlit base color, then tone mapping."); }
            else if (cornell && activeGraph.view==experiment::DisplayView::Final) {
                ImGui::TextUnformatted("Directional PBR; no area light/GI.");
                ImGui::TextUnformatted("Use Base color to inspect both walls.");
            }
            if (!graphDiagnostic.empty()) { ImGui::TextWrapped("%s",graphDiagnostic.c_str()); }
            if (!diagnostic.empty()) { ImGui::TextWrapped("%s",diagnostic.c_str()); }
            ImGui::PopItemWidth(); ImGui::End();
            if (edited) { request(draft); }
        };
        callbacks.keyPressed=[&](char key) {
            if (key=='R') { compileRequested=true; return; }
            if (key<'1' || key>'9') { return; }
            mode=key-'1';
            Controls draft;
            draft.exposure=mode==1 ? -2.f : mode==2 ? 2.f : 0.f;
            draft.irradiance=mode==3 ? 0.f : 4*3.14159265f;
            request(draft);
            setPose(mode);
        };
        callbacks.presented=[&] {
            if (activeGraph.normalMapping!=lastPresentedGraph.normalMapping) {
                std::fprintf(stderr,"CustomPass: normal map %s presented.\n",activeGraph.normalMapping ? "on" : "off");
            }
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
