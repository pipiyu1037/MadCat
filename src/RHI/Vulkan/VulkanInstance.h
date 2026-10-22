#pragma once

#include <vulkan/vulkan.h>

namespace madcat::rhi
{
// GLFW must outlive this owner. Destroy dependent Surfaces before the Instance.
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

    [[nodiscard]] VkInstance handle() const { return instance_; }
    [[nodiscard]] bool surfaceMaintenanceEnabled() const { return surfaceMaintenanceEnabled_; }

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    bool surfaceMaintenanceEnabled_ = false;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroyInstance_ = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugMessenger_ = nullptr;
};
}
