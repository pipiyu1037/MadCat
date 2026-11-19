#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "Runtime/Application.h"
#include "RenderScene/VulkanMaterials.h"
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
#include "RHI/Vulkan/VulkanUi.h"
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>

namespace madcat::runtime
{
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
    madcat::rhi::VulkanTrianglePipeline shadow;
    madcat::rhi::VulkanDepthViewPipeline depthView;
    madcat::rhi::VulkanClearRenderer renderer;
};

class GraphicsLoop
{
public:
    GraphicsLoop(GLFWwindow* window, const rhi::VulkanDevice& device, const rhi::VulkanSurface& surface,
        const rhi::VulkanShaderModule& vertex, const rhi::VulkanShaderModule& fragment,
        rhi::BufferRef vertices, rhi::BufferRef indices, std::vector<rhi::TextureBindingRef> materials,
        const rhi::VulkanShaderModule* depthVertex, const rhi::VulkanShaderModule* depthFragment,
        const rhi::VulkanShaderModule* shadowVertex, const rhi::VulkanShaderModule* shadowFragment,
        const ApplicationOptions& options, const GraphicsCallbacks& callbacks)
        : window_(window), device_(device), surface_(surface), vertex_(vertex), fragment_(fragment),
          vertices_(std::move(vertices)), indices_(std::move(indices)), materials_(std::move(materials)),
          depthVertex_(depthVertex), depthFragment_(depthFragment), shadowVertex_(shadowVertex), shadowFragment_(shadowFragment),
          options_(options), callbacks_(callbacks), activeGraph_(options.graphFactory), lastTime_(glfwGetTime())
    {
        iconified_ = glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
        glfwSetWindowUserPointer(window_, this);
        glfwSetKeyCallback(window_, [](GLFWwindow* target, int key, int, int action, int) {
            auto& app = *static_cast<GraphicsLoop*>(glfwGetWindowUserPointer(target));
            if (action != GLFW_PRESS || !app.callbacks_.keyPressed || (app.ui_ && app.ui_->capturesKeyboard())) { return; }
            // GLFW letter/digit key codes match ASCII; no platform key type escapes.
            if ((key >= GLFW_KEY_0 && key <= GLFW_KEY_9) || (key >= GLFW_KEY_A && key <= GLFW_KEY_Z))
            {
                try { app.callbacks_.keyPressed(static_cast<char>(key)); }
                catch (const std::exception& error) { std::fprintf(stderr, "MadCat input failed: %s.\n", error.what()); app.failed_ = true; }
                catch (...) { std::fprintf(stderr, "MadCat input failed: unknown exception.\n"); app.failed_ = true; }
            }
        });
        glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* target, int, int) {
            static_cast<GraphicsLoop*>(glfwGetWindowUserPointer(target))->rebuildPending_ = true;
        });
        glfwSetWindowIconifyCallback(window_, [](GLFWwindow* target, int iconified) {
            auto& app = *static_cast<GraphicsLoop*>(glfwGetWindowUserPointer(target));
            app.iconified_ = iconified == GLFW_TRUE;
            app.rebuildPending_ = true;
        });
        // Windows may run a native modal loop while dragging the window border.
        // Refresh callbacks still arrive there, even before PollEvents returns.
        glfwSetWindowRefreshCallback(window_, [](GLFWwindow* target) {
            static_cast<GraphicsLoop*>(glfwGetWindowUserPointer(target))->render();
        });
        if (callbacks_.ui)
        {
            ui_=std::make_unique<rhi::VulkanUi>();
            if (!ui_->initialize(window_)) { throw std::runtime_error("UI platform initialization failed"); }
        }
    }

    ~GraphicsLoop()
    {
        // Retire commands first. UI shutdown restores the chained callbacks,
        // which must happen before we clear the application's callbacks below.
        presentation_.reset();
        ui_.reset();
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
            if (ui_) { ui_->resetRenderer(); }
            auto candidate = std::make_unique<Presentation>();
            if (!candidate->swapchain.initialize(device_, surface_, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}) ||
                (ui_ && !ui_->prepare(device_,candidate->swapchain.surfaceFormat().format,static_cast<uint32_t>(candidate->swapchain.images().size()))) ||
                !(candidate->depth = madcat::rhi::createDepthTexture(device_, candidate->swapchain.extent().width, candidate->swapchain.extent().height)) ||
                !candidate->triangle.initialize(device_, options_.forwardColorFormat==rhi::ColorFormat::Rgba16Float ? VK_FORMAT_R16G16B16A16_SFLOAT :
                    candidate->swapchain.surfaceFormat().format, vertex_, fragment_, vertices_, indices_, madcat::rhi::detail::TextureAccess::format(candidate->depth), materials_) ||
                (depthVertex_ && !candidate->depthView.initialize(device_, candidate->swapchain.surfaceFormat().format, *depthVertex_, *depthFragment_)) ||
                (shadowVertex_ && !candidate->shadow.initialize(device_, VK_FORMAT_UNDEFINED, *shadowVertex_, *shadowFragment_, vertices_, indices_,
                    madcat::rhi::detail::TextureAccess::format(candidate->depth), materials_, true)) ||
                !candidate->renderer.initialize(device_, candidate->swapchain, candidate->depth,
                    activeGraph_,
                    &candidate->triangle, depthVertex_ ? &candidate->depthView : nullptr, shadowVertex_ ? &candidate->shadow : nullptr,ui_.get()))
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
        const auto extent = presentation_->swapchain.extent();
        const double now = glfwGetTime();
        const FrameInfo info{static_cast<float>(extent.width) / static_cast<float>(extent.height), now - lastTime_};
        lastTime_ = now;
        if (ui_) { ui_->frame(callbacks_.ui); }
        if (callbacks_.graphRequest)
        {
            if (auto request=callbacks_.graphRequest())
            {
                GraphUpdate update{request->id,false,{}};
                if (!request->id || request->id<=lastGraphRequest_)
                { update.diagnostic="Graph request ID must be nonzero and increasing"; }
                else
                {
                    lastGraphRequest_=request->id;
                    auto result=presentation_->renderer.replaceGraph(request->factory);
                    if (result.status==rhi::GraphReplaceStatus::Failed) { throw std::runtime_error(result.diagnostic); }
                    update.applied=result.status==rhi::GraphReplaceStatus::Applied;
                    update.diagnostic=std::move(result.diagnostic);
                    if (update.applied) { activeGraph_=std::move(request->factory); }
                }
                std::fprintf(stderr,"MadCat: graph request %llu %s%s%s.\n",static_cast<unsigned long long>(update.id),
                    update.applied ? "applied" : "rejected",update.diagnostic.empty() ? "" : ": ",update.diagnostic.c_str());
                if (callbacks_.graphUpdated) { callbacks_.graphUpdated(update); }
            }
        }
        auto frame = callbacks_.frame(info);
        if (!frame) { throw std::runtime_error(frame.error()); }
        const auto result = presentation_->renderer.drawIndexed(frame->clearColor, frame->draws, frame->lighting, frame->shadowDraws);
        if (result == rhi::ClearFrameResult::Presented && callbacks_.presented) { callbacks_.presented(); }
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
    std::vector<madcat::rhi::TextureBindingRef> materials_;
    const madcat::rhi::VulkanShaderModule* depthVertex_;
    const madcat::rhi::VulkanShaderModule* depthFragment_;
    const madcat::rhi::VulkanShaderModule* shadowVertex_;
    const madcat::rhi::VulkanShaderModule* shadowFragment_;
    const ApplicationOptions& options_;
    const GraphicsCallbacks& callbacks_;
    graph::GraphicsGraphFactory activeGraph_;
    uint64_t lastGraphRequest_=0;
    double lastTime_ = 0;
    std::unique_ptr<rhi::VulkanUi> ui_;
    std::unique_ptr<Presentation> presentation_;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    bool rebuildPending_ = true;
    bool iconified_ = false;
    bool paused_ = false;
    bool rendering_ = false;
    bool failed_ = false;
};
}

int runGraphics(const ApplicationOptions& options, const assets::ModelAssets& assets,
    const render::GeometryBatch& geometry, const GraphicsCallbacks& callbacks)
{
    if (!options.width || !options.height || options.width > INT_MAX || options.height > INT_MAX ||
        !options.graphFactory || !callbacks.frame || geometry.vertices.empty() || geometry.indices.empty() ||
        (options.forwardColorFormat!=rhi::ColorFormat::Presentation && options.forwardColorFormat!=rhi::ColorFormat::Rgba16Float))
    {
        std::fprintf(stderr, "MadCat error: invalid application extent, graph, frame callback or startup geometry.\n");
        return EXIT_FAILURE;
    }
    glfwSetErrorCallback([](int code, const char* description) {
        std::fprintf(stderr, "GLFW error (%d): %s\n", code, description);
    });
    if (glfwInit() != GLFW_TRUE) { return EXIT_FAILURE; }
    struct GlfwLifetime { ~GlfwLifetime() { glfwTerminate(); } } glfwLifetime;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
        glfwCreateWindow(static_cast<int>(options.width), static_cast<int>(options.height), options.title.c_str(), nullptr, nullptr),
        glfwDestroyWindow);
    if (!window) { return EXIT_FAILURE; }
    try
    {
        rhi::VulkanInstance vulkan;
        rhi::VulkanSurface surface;
        rhi::VulkanDevice device;
        if (!vulkan.initialize() || !surface.initialize(vulkan, window.get()) || !initializeDevice(vulkan, surface, device)) { return EXIT_FAILURE; }
        const auto shaderDirectory = options.shaderDirectory.empty() ?
            std::filesystem::path(u8"" MADCAT_SHADER_DIRECTORY) : options.shaderDirectory;
        rhi::VulkanShaderModule vertex, fragment, depthVertex, depthFragment, shadowVertex, shadowFragment;
        if (options.prepareShadow && (!shadowVertex.initialize(device, shaderDirectory.parent_path()/"shadow/vertex.spv") ||
            !shadowFragment.initialize(device, shaderDirectory.parent_path()/"shadow/fragment.spv")))
        { throw std::runtime_error("Shadow Shader initialization failed; build MadCatShaders then restart"); }
        if (options.prepareDepthView && (!depthVertex.initialize(device, shaderDirectory.parent_path()/"depth-view/vertex.spv") ||
            !depthFragment.initialize(device, shaderDirectory.parent_path()/"depth-view/fragment.spv")))
        { throw std::runtime_error("Depth viewer Shader initialization failed; build MadCatShaders then restart"); }
        if (!vertex.initialize(device, shaderDirectory/"vertex.spv") || !fragment.initialize(device, shaderDirectory/"fragment.spv"))
        { throw std::runtime_error("Shader initialization failed; build MadCatShaders then restart"); }
        std::fprintf(stderr, "MadCat: triangle Shader Modules ready (vertexMain / fragmentMain).\n");
        auto vertices = rhi::createVertexBuffer(device, std::as_bytes(std::span(geometry.vertices)));
        if (!vertices) { throw std::runtime_error("Vertex upload failed"); }
        auto indices = rhi::createIndexBuffer(device, geometry.indices);
        if (!indices) { throw std::runtime_error("Index upload failed"); }
        auto materials = render::prepareMaterialBindings(device, assets, geometry.materials);
        if (!materials) { throw std::runtime_error(materials.error()); }
        GraphicsLoop app(window.get(), device, surface, vertex, fragment, std::move(vertices), std::move(indices),
            std::move(*materials), options.prepareDepthView ? &depthVertex : nullptr, options.prepareDepthView ? &depthFragment : nullptr,
            options.prepareShadow ? &shadowVertex : nullptr, options.prepareShadow ? &shadowFragment : nullptr, options, callbacks);
        while (!glfwWindowShouldClose(window.get()) && !app.failed())
        {
            glfwPollEvents();
            if (glfwWindowShouldClose(window.get()) || app.failed()) { break; }
            app.render();
            if (app.paused() && !app.failed()) { glfwWaitEvents(); }
        }
        return !app.failed() && app.waitIdle() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "MadCat application failed: %s\n", error.what()); }
    catch (...) { std::fprintf(stderr, "MadCat application failed: unknown exception.\n"); }
    return EXIT_FAILURE;
}
}
