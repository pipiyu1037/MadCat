#pragma once

#include <vulkan/vulkan.h>

namespace madcat::rhi
{
// GLFW must remain initialized until this owner has been destroyed.
class VulkanInstance
{
public:
    VulkanInstance() = default;
    ~VulkanInstance();

    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;
    VulkanInstance(VulkanInstance&&) = delete;
    VulkanInstance& operator=(VulkanInstance&&) = delete;

    // Call once. Any resources created before a failure remain owned here.
    [[nodiscard]] bool initialize();

    // Query support only; no device is selected and no features are enabled.
    [[nodiscard]] bool reportPhysicalDevices() const;

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroyInstance_ = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger_ = nullptr;
};
}
