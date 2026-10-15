#pragma once

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace madcat::rhi
{
class VulkanInstance;

// Borrows the Instance and window. Both must outlive this owner.
// Any future swapchain for this Surface must be destroyed first.
class VulkanSurface
{
public:
    VulkanSurface() = default;
    ~VulkanSurface();

    VulkanSurface(const VulkanSurface&) = delete;
    VulkanSurface& operator=(const VulkanSurface&) = delete;
    VulkanSurface(VulkanSurface&&) = delete;
    VulkanSurface& operator=(VulkanSurface&&) = delete;

    [[nodiscard]] bool initialize(const VulkanInstance& instance, GLFWwindow* window);
    [[nodiscard]] VkSurfaceKHR handle() const { return surface_; }
    [[nodiscard]] VkInstance instanceHandle() const { return instance_; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    PFN_vkDestroySurfaceKHR destroySurface_ = nullptr;
};
}
