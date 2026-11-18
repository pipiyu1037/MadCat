#include "Runtime/Application.h"
#include "ToneMapPass.h"
#include "Passes/UiPass.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    using namespace madcat;
    try
    {
        if (argc>2) { throw std::runtime_error("Usage: CustomPass [model.gltf|model.glb]"); }
        const auto path=argc==2 ? std::filesystem::path(argv[1]) :
            std::filesystem::path(u8"" MADCAT_EXAMPLE_ASSETS)/"models/Suzanne/Suzanne.gltf";
        auto imported=assets::importGltfGeometry(path);
        if (!imported) { throw std::runtime_error(imported.error().message); }
        assets::ModelAssets assets;
        const auto model=assets.add(std::move(*imported));
        scene::Scene scene;
        const auto instance=scene::instantiateModel(scene,assets,model);
        if (!instance) { throw std::runtime_error(instance.error()); }
        const auto camera=scene.create(), light=scene.create();
        scene::LocalTransform modelPose, cameraPose;
        modelPose.scale=glm::vec3(.7f); cameraPose.translation.z=3;
        if (!scene.setLocalTransform(instance->root,modelPose) || !scene.add<scene::PerspectiveCamera>(camera) ||
            !scene.setLocalTransform(camera,cameraPose)) { throw std::runtime_error("Scene setup failed"); }
        auto* sunlight=scene.add<scene::DirectionalLight>(light);
        if (!sunlight) { throw std::runtime_error("Light setup failed"); }
        sunlight->color={1,.8f,.6f}; sunlight->irradiance=4*3.14159265f;

        experiment::ToneMapPass::Parameters displayParameters;
        runtime::ApplicationOptions application;
        application.title="MadCat CustomPass";
        application.forwardColorFormat=rhi::ColorFormat::Rgba16Float;
        application.graphFactory=[&](graph::Graph& graph, const graph::GraphicsImports& input)
            -> std::expected<graph::GraphicsGraph,std::string> {
            if (!input.presentationSrgb) { return std::unexpected("CustomPass display Shader requires an sRGB presentation attachment"); }
            // Prepare the experiment's Shader/layout/pipeline using RHI only.
            auto prepared=experiment::ToneMapPass::prepare(input.prepareGraphics,
                std::filesystem::path(u8"" MADCAT_CUSTOM_SHADERS));
            if (!prepared) { return std::unexpected(prepared.error()); }
            const auto hdr=graph.createColorTexture("SceneHDR",{input.width,input.height});
            const auto clear=graph.addPass("ClearHDR",passes::ClearPass{},passes::Targets{hdr,input.depth});
            const auto forward=graph.addPass("ForwardHDR",passes::ForwardPass{},passes::ForwardPass::Inputs{
                clear.outputs,input.vertices,input.indices,input.lighting,input.textures});
            const auto toneMap=graph.addPass("ToneMap",experiment::ToneMapPass{},
                experiment::ToneMapPass::Inputs{forward.outputs.color,input.color},std::move(*prepared));
            const auto ui=graph.addPass("UI",passes::UiPass{},passes::UiPass::Inputs{toneMap.outputs.color,input.uiFont});
            const auto present=graph.addPass("Present",passes::PresentPass{},passes::PresentPass::Inputs{ui.outputs.color});
            auto plan=graph.compile();
            if (!plan) { return std::unexpected(plan.error()); }
            auto parameters=[&,clearNode=clear.node,forwardNode=forward.node,toneNode=toneMap.node,
                uiNode=ui.node,presentNode=present.node,count=plan->nodes().size()](const std::array<float,4>& color,
                    std::span<const rhi::IndexedDraw> draws,std::span<const rhi::IndexedDraw>) {
                std::vector<std::any> values(count);
                values[clearNode]=passes::ClearPass::Parameters{color};
                values[forwardNode]=passes::ForwardPass::Parameters{{draws.begin(),draws.end()}};
                values[toneNode]=displayParameters; // copied again at the frame's record boundary
                values[uiNode]=passes::UiPass::Parameters{};
                values[presentNode]=passes::PresentPass::Parameters{};
                return values;
            };
            return graph::GraphicsGraph{std::move(*plan),std::move(parameters)};
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
        runtime::SceneCallbacks callbacks;
        callbacks.ui=[&] {
            auto draft=controls;
            ImGui::SetNextWindowPos({12,12},ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize({340,210},ImGuiCond_Always);
            ImGui::Begin("CustomPass Parameters",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::PushItemWidth(185);
            bool edited=ImGui::SliderFloat("Exposure (EV)",&draft.exposure,-8,8,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            edited|=ImGui::ColorEdit3("Light color",draft.color.data(),ImGuiColorEditFlags_Float|ImGuiColorEditFlags_NoPicker|ImGuiColorEditFlags_NoOptions);
            edited|=ImGui::SliderFloat("Irradiance",&draft.irradiance,0,32,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::Button("Restore defaults"))
            {
                draft=Controls{}; edited=true; mode=0;
                modelPose.translation.x=0;
                if (!scene.setLocalTransform(instance->root,modelPose)) { throw std::runtime_error("Scene edit failed"); }
            }
            ImGui::TextUnformatted("Ctrl+click a slider to type a value.");
            if (!diagnostic.empty()) { ImGui::TextWrapped("%s",diagnostic.c_str()); }
            ImGui::PopItemWidth(); ImGui::End();
            if (edited) { request(draft); }
        };
        callbacks.keyPressed=[&](char key) {
            if (key<'1' || key>'5') { return; }
            mode=key-'1';
            Controls draft;
            draft.exposure=mode==1 ? -2.f : mode==2 ? 2.f : 0.f;
            draft.irradiance=mode==3 ? 0.f : 4*3.14159265f;
            request(draft);
            modelPose.translation.x=mode==4 ? .45f : 0;
            if (!scene.setLocalTransform(instance->root,modelPose)) { throw std::runtime_error("Scene edit failed"); }
        };
        callbacks.presented=[&] {
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
