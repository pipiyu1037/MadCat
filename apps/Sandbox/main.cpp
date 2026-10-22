#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "RHI/Vulkan/VulkanDevice.h"
#include "RHI/Vulkan/VulkanClearRenderer.h"
#include "RHI/Vulkan/VulkanInstance.h"
#include "RHI/Vulkan/VulkanPhysicalDevice.h"
#include "RHI/Vulkan/VulkanShaderModule.h"
#include "RHI/Vulkan/VulkanSurface.h"
#include "RHI/Vulkan/VulkanSwapchain.h"
#include "RHI/Vulkan/VulkanTrianglePipeline.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
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
        candidates.push_back({std::move(devices[deviceIndex]), std::move(support)});
    }
    std::fprintf(stderr, "Vulkan: physical device capability report complete.\n");
    return device.initialize(candidates);
}

// Renderer waits for GPU/WSI work and releases commands before the pipeline
// and Swapchain it borrows are destroyed (reverse member declaration order).
struct Presentation
{
    madcat::rhi::VulkanSwapchain swapchain;
    madcat::rhi::VulkanTrianglePipeline triangle;
    madcat::rhi::VulkanClearRenderer renderer;
};

class SandboxApplication
{
public:
    SandboxApplication(GLFWwindow* window, const madcat::rhi::VulkanDevice& device, const madcat::rhi::VulkanSurface& surface,
        const madcat::rhi::VulkanShaderModule& vertex, const madcat::rhi::VulkanShaderModule& fragment)
        : window_(window), device_(device), surface_(surface), vertex_(vertex), fragment_(fragment)
    {
        iconified_ = glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
        glfwSetWindowUserPointer(window_, this);
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
                !candidate->triangle.initialize(device_, candidate->swapchain.surfaceFormat().format, vertex_, fragment_) ||
                !candidate->renderer.initialize(device_, candidate->swapchain, &candidate->triangle))
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
        const auto result = presentation_->renderer.draw({0.04f, 0.08f, 0.16f, 1.0f});
        if (result == madcat::rhi::ClearFrameResult::Failed) { return false; }
        rebuildPending_ |= result == madcat::rhi::ClearFrameResult::SwapchainUnavailable;
        return true;
    }

    GLFWwindow* window_;
    const madcat::rhi::VulkanDevice& device_;
    const madcat::rhi::VulkanSurface& surface_;
    const madcat::rhi::VulkanShaderModule& vertex_;
    const madcat::rhi::VulkanShaderModule& fragment_;
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

int main()
{
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
            if (vertexShader.initialize(device, shaderDirectory / "vertex.spv") &&
                fragmentShader.initialize(device, shaderDirectory / "fragment.spv"))
            {
                std::fprintf(stderr, "MadCat: triangle Shader Modules ready (vertexMain / fragmentMain).\n");
                SandboxApplication app(window, device, surface, vertexShader, fragmentShader);
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
