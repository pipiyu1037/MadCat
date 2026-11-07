#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "RHI/Vulkan/VulkanDevice.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanDepthTexture.h"
#include "RHI/Vulkan/VulkanDepthViewPipeline.h"
#include "RHI/Vulkan/VulkanClearRenderer.h"
#include "RHI/Vulkan/VulkanInstance.h"
#include "RHI/Vulkan/VulkanPhysicalDevice.h"
#include "RHI/Vulkan/VulkanShaderModule.h"
#include "RHI/Vulkan/VulkanSurface.h"
#include "RHI/Vulkan/VulkanSwapchain.h"
#include "RHI/Vulkan/VulkanTrianglePipeline.h"
#include "TriangleScene.h"
#include "ModelScene.h"
#include "Passes/ForwardPass.h"
#include "Passes/DepthViewPass.h"
#include "ModelMaterials.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
bool initializeDevice(const madcat::rhi::VulkanInstance& instance, const madcat::rhi::VulkanSurface& surface, madcat::rhi::VulkanDevice& device)
{
    std::vector<madcat::rhi::PhysicalDeviceInfo> devices;
    if (!madcat::rhi::queryPhysicalDevices(instance, devices))
    {
        return false;
    }
    std::fprintf(stderr, "Vulkan: found %zu physical device(s).\n", devices.size());
    std::vector<madcat::rhi::PhysicalDeviceCandidate> candidates;
    candidates.reserve(devices.size());
    for (size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex)
    {
        madcat::rhi::SurfaceSupport support;
        if (!madcat::rhi::querySurfaceSupport(devices[deviceIndex], surface, support))
        {
            return false;
        }
        madcat::rhi::printPhysicalDeviceReport(devices[deviceIndex], support, deviceIndex);
        if (!devices[deviceIndex].features.shaderDemoteToHelperInvocation.supported)
        {
            std::fprintf(stderr,"MadCat: device excluded; material Shader requires shaderDemoteToHelperInvocation.\n");
            continue;
        }
        candidates.push_back({std::move(devices[deviceIndex]), std::move(support)});
    }
    std::fprintf(stderr, "Vulkan: physical device capability report complete.\n");
    return device.initialize(candidates);
}

// Renderer waits and releases commands before the pipeline, depth texture and
// Swapchain are released (reverse member declaration order).
struct Presentation
{
    madcat::rhi::VulkanSwapchain swapchain;
    madcat::rhi::TextureRef depth;
    madcat::rhi::VulkanTrianglePipeline triangle;
    madcat::rhi::VulkanDepthViewPipeline depthView;
    madcat::rhi::VulkanClearRenderer renderer;
};

class SandboxApplication
{
public:
    SandboxApplication(GLFWwindow* window, const madcat::rhi::VulkanDevice& device, const madcat::rhi::VulkanSurface& surface,
        const madcat::rhi::VulkanShaderModule& vertex, const madcat::rhi::VulkanShaderModule& fragment,
        madcat::rhi::BufferRef vertices, madcat::rhi::BufferRef indices,
        madcat::sandbox::ModelScene* modelScene, const madcat::render::GeometryBatch* geometry,
        std::vector<madcat::rhi::TextureBindingRef> materials,
        const madcat::rhi::VulkanShaderModule* depthVertex, const madcat::rhi::VulkanShaderModule* depthFragment)
        : window_(window), device_(device), surface_(surface), vertex_(vertex), fragment_(fragment),
          vertices_(std::move(vertices)), indices_(std::move(indices)), modelScene_(modelScene), geometry_(geometry),
          materials_(std::move(materials)), depthVertex_(depthVertex), depthFragment_(depthFragment),
          requestedView_(modelScene ? madcat::rhi::GeometryView::Material : madcat::rhi::GeometryView::Color)
    {
        iconified_ = glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
        glfwSetWindowUserPointer(window_, this);
        glfwSetKeyCallback(window_, [](GLFWwindow* target, int key, int, int action, int) {
            auto* app = static_cast<SandboxApplication*>(glfwGetWindowUserPointer(target));
            if (action == GLFW_PRESS && key >= GLFW_KEY_1 && key <= (app->modelScene_ ? GLFW_KEY_9 : GLFW_KEY_7))
            {
                app->requestedPose_ = key - GLFW_KEY_1;
            }
            if (action == GLFW_PRESS && key == GLFW_KEY_C) { app->requestedView_ = madcat::rhi::GeometryView::Color; }
            if (action == GLFW_PRESS && key == GLFW_KEY_N) { app->requestedView_ = madcat::rhi::GeometryView::WorldNormal; }
            if (action == GLFW_PRESS && key == GLFW_KEY_U) { app->requestedView_ = madcat::rhi::GeometryView::Texcoord; }
            if (action == GLFW_PRESS && key == GLFW_KEY_M) { app->requestedView_ = madcat::rhi::GeometryView::Material; }
            if (action == GLFW_PRESS && key == GLFW_KEY_L && app->modelScene_) { app->requestedView_ = madcat::rhi::GeometryView::Lit; }
            if (action == GLFW_PRESS && key == GLFW_KEY_J && app->modelScene_) { app->requestedLighting_ = (app->requestedLighting_ + 1) % 3; }
            if (action == GLFW_PRESS && key == GLFW_KEY_0 && app->modelScene_) { app->requestedPose_ = 9; }
        });
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* target, int, int) {
            static_cast<SandboxApplication*>(glfwGetWindowUserPointer(target))->rebuildPending_ = true;
        });
        glfwSetWindowIconifyCallback(window_, [](GLFWwindow* target, int iconified) {
            auto& app = *static_cast<SandboxApplication*>(glfwGetWindowUserPointer(target));
            app.iconified_ = iconified == GLFW_TRUE;
            app.rebuildPending_ = true;
        });
        // Windows may run a native modal loop while dragging the window border.
        // Refresh callbacks still arrive there, even before PollEvents returns.
        glfwSetWindowRefreshCallback(window_, [](GLFWwindow* target) {
            static_cast<SandboxApplication*>(glfwGetWindowUserPointer(target))->render();
        });
    }

    ~SandboxApplication()
    {
        // No callback may retain this owner during destruction/window teardown.
        glfwSetWindowRefreshCallback(window_, nullptr);
        glfwSetKeyCallback(window_, nullptr);
        glfwSetFramebufferSizeCallback(window_, nullptr);
        glfwSetWindowIconifyCallback(window_, nullptr);
        glfwSetWindowUserPointer(window_, nullptr);
    }

    [[nodiscard]] bool failed() const { return failed_; }
    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] bool waitIdle() { return !presentation_ || presentation_->renderer.waitIdle(); }

    // This function is also a C callback boundary. Do not let allocation or
    // other C++ exceptions escape through GLFW/Win32, or recursively render.
    void render() noexcept
    {
        if (rendering_ || failed_ || glfwWindowShouldClose(window_)) { return; }
        rendering_ = true;
        try
        {
            if (!renderFrame()) { failed_ = true; }
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "MadCat rendering failed: %s\n", error.what());
            failed_ = true;
        }
        catch (...)
        {
            std::fprintf(stderr, "MadCat rendering failed: unknown exception.\n");
            failed_ = true;
        }
        rendering_ = false;
    }

private:
    bool renderFrame()
    {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        if (iconified_ || width <= 0 || height <= 0)
        {
            if (!paused_)
            {
                if (!waitIdle()) { return false; }
                std::fprintf(stderr, "MadCat: rendering paused; minimized or zero framebuffer.\n");
            }
            paused_ = true;
            rebuildPending_ = true;
            return true;
        }
        paused_ = false;
        if (rebuildPending_ || width != framebufferWidth_ || height != framebufferHeight_)
        {
            if (!waitIdle()) { return false; }
            // Destroy the old native-window association before creating a new
            // Swapchain. No oldSwapchain retirement/reuse optimization yet.
            presentation_.reset();
            auto candidate = std::make_unique<Presentation>();
            if (!candidate->swapchain.initialize(device_, surface_, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}) ||
                !(candidate->depth = madcat::rhi::createDepthTexture(device_, candidate->swapchain.extent().width, candidate->swapchain.extent().height)) ||
                !candidate->triangle.initialize(device_, candidate->swapchain.surfaceFormat().format, vertex_, fragment_, vertices_, indices_, madcat::rhi::detail::TextureAccess::format(candidate->depth), materials_) ||
                (depthVertex_ && !candidate->depthView.initialize(device_, candidate->swapchain.surfaceFormat().format, *depthVertex_, *depthFragment_)) ||
                !candidate->renderer.initialize(device_, candidate->swapchain, candidate->depth,
                    depthVertex_ ? madcat::passes::buildDepthViewGraph : madcat::passes::buildForwardGraph,
                    &candidate->triangle, depthVertex_ ? &candidate->depthView : nullptr))
            {
                std::fprintf(stderr, "MadCat error: presentation creation/rebuild failed; stopping rendering.\n");
                return false;
            }
            presentation_ = std::move(candidate);
            framebufferWidth_ = width;
            framebufferHeight_ = height;
            rebuildPending_ = false;
            const auto extent = presentation_->swapchain.extent();
            std::fprintf(stderr, "MadCat: presentation ready; framebuffer %d x %d, Swapchain %u x %u.\n",
                width, height, extent.width, extent.height);
        }
        if (activePose_ != requestedPose_)
        {
            if (modelScene_ ? !modelScene_->selectPose(requestedPose_) :
                !triangleScene_.selectPose(static_cast<madcat::sandbox::TrianglePose>(requestedPose_))) { return false; }
            activePose_ = requestedPose_;
        }
        const auto extent = presentation_->swapchain.extent();
        madcat::rhi::ClearFrameResult result;
        const auto aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        if (modelScene_)
        {
            if (activeLighting_ != requestedLighting_)
            {
                if (!modelScene_->selectLighting(requestedLighting_)) { return false; }
                activeLighting_ = requestedLighting_;
            }
            auto snapshot = modelScene_->draws(aspect);
            if (!snapshot) { throw std::runtime_error(snapshot.error()); }
            auto draws = madcat::render::resolveDraws(*geometry_, *snapshot);
            if (!draws) { throw std::runtime_error(draws.error()); }
            for (auto& draw : *draws) { draw.transform.view = requestedView_; }
            const auto lighting = modelScene_->lighting();
            if (!lighting) { throw std::runtime_error(lighting.error()); }
            result = presentation_->renderer.drawIndexed({0.04f, 0.08f, 0.16f, 1.0f}, *draws, *lighting);
            if (result == madcat::rhi::ClearFrameResult::Presented && loggedLighting_ != activeLighting_)
            {
                loggedLighting_ = activeLighting_;
                std::fprintf(stderr, "MadCat: lighting presented; %d (J=default/rotated/off).\n", loggedLighting_);
            }
            if (result == madcat::rhi::ClearFrameResult::Presented && loggedPose_ != activePose_)
            {
                std::fprintf(stderr, "MadCat: model instances presented; pose %d, %zu draws, %zu shared primitives.\n",
                    activePose_ + 1, draws->size(), geometry_->ranges.size());
            }
        }
        else
        {
            auto transform = triangleScene_.drawTransforms(aspect);
            if (!transform) { throw std::runtime_error("Scene camera/model matrix extraction failed"); }
            for (auto& draw : transform->transforms) { draw.view = requestedView_; }
            result = presentation_->renderer.draw({0.04f, 0.08f, 0.16f, 1.0f},
                std::span(transform->transforms.data(), transform->count));
        }
        if (result == madcat::rhi::ClearFrameResult::Presented && loggedPose_ != activePose_)
        {
            std::fprintf(stderr, "MadCat: Scene pose presented; %d (keys 1-%d).\n", activePose_ + 1, modelScene_ ? 9 : 7);
            loggedPose_ = activePose_;
        }
        if (result == madcat::rhi::ClearFrameResult::Presented && loggedView_ != static_cast<int>(requestedView_))
        {
            loggedView_ = static_cast<int>(requestedView_);
            std::fprintf(stderr, "MadCat: geometry view presented; %d (L=lit, M=material, C=color, N=world normal, U=UV; magenta=unavailable).\n", loggedView_);
        }
        if (result == madcat::rhi::ClearFrameResult::Failed) { return false; }
        rebuildPending_ |= result == madcat::rhi::ClearFrameResult::SwapchainUnavailable;
        return true;
    }

    GLFWwindow* window_;
    const madcat::rhi::VulkanDevice& device_;
    const madcat::rhi::VulkanSurface& surface_;
    const madcat::rhi::VulkanShaderModule& vertex_;
    const madcat::rhi::VulkanShaderModule& fragment_;
    madcat::rhi::BufferRef vertices_;
    madcat::rhi::BufferRef indices_;
    madcat::sandbox::TriangleScene triangleScene_;
    madcat::sandbox::ModelScene* modelScene_;
    const madcat::render::GeometryBatch* geometry_;
    std::vector<madcat::rhi::TextureBindingRef> materials_;
    const madcat::rhi::VulkanShaderModule* depthVertex_;
    const madcat::rhi::VulkanShaderModule* depthFragment_;
    madcat::rhi::GeometryView requestedView_ = madcat::rhi::GeometryView::Color;
    int loggedView_ = -1;
    int requestedLighting_ = 0, activeLighting_ = 0, loggedLighting_ = -1;
    std::unique_ptr<Presentation> presentation_;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    bool rebuildPending_ = true;
    bool iconified_ = false;
    bool paused_ = false;
    bool rendering_ = false;
    bool failed_ = false;
    int requestedPose_ = 0;
    int activePose_ = 0;
    int loggedPose_ = -1;
};
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    madcat::assets::ModelAssets assets;
    std::unique_ptr<madcat::sandbox::ModelScene> modelScene;
    std::optional<madcat::render::GeometryBatch> model;
    bool depthView = false;
    try
    {
        std::optional<std::filesystem::path> modelPath;
        for (int argument=1; argument<argc; ++argument)
        {
            const std::filesystem::path option(argv[argument]);
            if (option=="--depth-view" && !depthView) { depthView=true; }
            else if (option=="--gltf" && !modelPath && argument+1<argc) { modelPath=argv[++argument]; }
            else
            {
                std::fprintf(stderr, "Usage: MadCat [--gltf <model.gltf|model.glb>] [--depth-view]\n");
                return EXIT_FAILURE;
            }
        }
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
    glfwSetErrorCallback([](int code, const char* description) {
        std::fprintf(stderr, "GLFW error (%d): %s\n", code, description);
    });

    if (glfwInit() != GLFW_TRUE)
    {
        return EXIT_FAILURE;
    }

    // Vulkan provides rendering; this window needs no OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "MadCat", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    int exitCode = EXIT_FAILURE;
    try
    {
        madcat::rhi::VulkanInstance vulkan;
        madcat::rhi::VulkanSurface surface;
        madcat::rhi::VulkanDevice device;
        if (vulkan.initialize() && surface.initialize(vulkan, window) && initializeDevice(vulkan, surface, device))
        {
            // Modules belong to the Device, independently of Swapchain rebuilds.
            const std::filesystem::path shaderDirectory(u8"" MADCAT_SHADER_DIRECTORY);
            madcat::rhi::VulkanShaderModule vertexShader;
            madcat::rhi::VulkanShaderModule fragmentShader;
            madcat::rhi::VulkanShaderModule depthVertex, depthFragment;
            if (depthView && (!depthVertex.initialize(device, shaderDirectory.parent_path()/"depth-view/vertex.spv") ||
                !depthFragment.initialize(device, shaderDirectory.parent_path()/"depth-view/fragment.spv")))
            { throw std::runtime_error("Depth viewer Shader initialization failed; build MadCatShaders then restart"); }
            if (vertexShader.initialize(device, shaderDirectory / "vertex.spv") &&
                fragmentShader.initialize(device, shaderDirectory / "fragment.spv"))
            {
                std::fprintf(stderr, "MadCat: triangle Shader Modules ready (vertexMain / fragmentMain).\n");
                constexpr madcat::rhi::ColoredVertex geometry[] = {
                    // Center is shared by three triangles. Its color is the
                    // barycentric average, preserving the original gradient.
                    {{0.0f, -0.2f, 0}, {1.0f / 3, 1.0f / 3, 1.0f / 3}},
                    {{0.6f, -0.6f, 0}, {0, 1, 0}}, {{0.0f, 0.6f, 0}, {1, 0, 0}}, {{-0.6f, -0.6f, 0}, {0, 0, 1}}
                };
                constexpr uint32_t triangleIndices[] = {2, 3, 0, 3, 1, 0, 1, 2, 0};
                const auto vertexData = model ? std::span<const madcat::rhi::ColoredVertex>(model->vertices) : std::span<const madcat::rhi::ColoredVertex>(geometry);
                const auto indexData = model ? std::span<const uint32_t>(model->indices) : std::span<const uint32_t>(triangleIndices);
                auto vertices = madcat::rhi::createVertexBuffer(device, std::as_bytes(vertexData));
                if (!vertices) { throw std::runtime_error("Triangle vertex upload failed"); }
                auto indices = madcat::rhi::createIndexBuffer(device, indexData);
                if (!indices) { throw std::runtime_error("Triangle index upload failed"); }
                auto materials = madcat::sandbox::prepareMaterialBindings(device, assets, model ? std::span<const madcat::render::MaterialData>(model->materials) : std::span<const madcat::render::MaterialData>{});
                if (!materials) { throw std::runtime_error(materials.error()); }
                SandboxApplication app(window, device, surface, vertexShader, fragmentShader, std::move(vertices), std::move(indices),
                    modelScene.get(), model ? &*model : nullptr, std::move(*materials),
                    depthView ? &depthVertex : nullptr, depthView ? &depthFragment : nullptr);
                exitCode = EXIT_SUCCESS;
                while (!glfwWindowShouldClose(window) && !app.failed())
                {
                    glfwPollEvents();
                    if (glfwWindowShouldClose(window)) { break; }
                    app.render();
                    if (app.paused() && !app.failed()) { glfwWaitEvents(); }
                }
                if (app.failed() || !app.waitIdle()) { exitCode = EXIT_FAILURE; }
            }
            else
            {
                std::fprintf(stderr, "MadCat error: Shader initialization failed. Run cmake --build <build-directory> --config Debug --target MadCatShaders, then restart.\n");
            }
        }
        // Renderer waits for acquisition, submission and present-resource
        // retirement before releasing commands/views/semaphores, then the
        // triangle pipeline and Swapchain are freed, followed by Shader Modules.
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "MadCat initialization failed: %s\n", error.what());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
