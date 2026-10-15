#include "VulkanSurface.h"

#include "VulkanFunctions.h"
#include "VulkanInstance.h"

namespace madcat::rhi
{
VulkanSurface::~VulkanSurface()
{
    if (surface_ != VK_NULL_HANDLE)
    {
        destroySurface_(instance_, surface_, nullptr);
        std::fprintf(stderr, "Vulkan: Surface destroyed.\n");
    }
}

bool VulkanSurface::initialize(const VulkanInstance& instance, GLFWwindow* window)
{
    if (instance.handle() == VK_NULL_HANDLE || !window)
    {
        std::fprintf(stderr, "Vulkan error: Surface creation requires an initialized Instance and a window.\n");
        return false;
    }
    if (surface_ != VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: this Surface owner is already initialized.\n");
        return false;
    }
    // Resolve destruction before acquiring ownership, including failure paths.
    const auto destroySurface = detail::loadFunction<PFN_vkDestroySurfaceKHR>(instance.handle(), "vkDestroySurfaceKHR");
    if (!destroySurface)
    {
        return false;
    }
    VkSurfaceKHR createdSurface = VK_NULL_HANDLE;
    if (!detail::checkResult(glfwCreateWindowSurface(instance.handle(), window, nullptr, &createdSurface), "glfwCreateWindowSurface"))
    {
        return false;
    }
    instance_ = instance.handle();
    surface_ = createdSurface;
    destroySurface_ = destroySurface;
    std::fprintf(stderr, "Vulkan: window Surface created.\n");
    return true;
}
}
