#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "RHI/Vulkan/VulkanInstance.h"
#include "RHI/Vulkan/VulkanPhysicalDevice.h"
#include "RHI/Vulkan/VulkanSurface.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

namespace
{
bool reportPhysicalDevices(const madcat::rhi::VulkanInstance& instance, const madcat::rhi::VulkanSurface& surface)
{
    std::vector<madcat::rhi::PhysicalDeviceInfo> devices;
    if (!madcat::rhi::queryPhysicalDevices(instance, devices))
    {
        return false;
    }
    std::fprintf(stderr, "Vulkan: found %zu physical device(s).\n", devices.size());
    for (size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex)
    {
        madcat::rhi::SurfaceSupport support;
        if (!madcat::rhi::querySurfaceSupport(devices[deviceIndex], surface, support))
        {
            return false;
        }
        madcat::rhi::printPhysicalDeviceReport(devices[deviceIndex], support, deviceIndex);
    }
    std::fprintf(stderr, "Vulkan: physical device capability report complete; no device selected or features enabled.\n");
    return true;
}
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

    // Vulkan will provide rendering later; this window needs no OpenGL context.
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
        if (vulkan.initialize() && surface.initialize(vulkan, window) && reportPhysicalDevices(vulkan, surface))
        {
            while (!glfwWindowShouldClose(window))
            {
                glfwWaitEvents();
            }
            exitCode = EXIT_SUCCESS;
        }
        // Surface and Instance are destroyed before the window and GLFW loader.
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "MadCat initialization failed: %s\n", error.what());
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
