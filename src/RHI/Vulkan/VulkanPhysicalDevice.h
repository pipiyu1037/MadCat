#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace madcat::rhi
{
class VulkanInstance;
class VulkanSurface;

struct FeatureSupport
{
    bool queryable = false;
    bool supported = false;
};

struct PhysicalDeviceFeatures
{
    FeatureSupport dynamicRendering;
    FeatureSupport synchronization2;
    FeatureSupport bufferDeviceAddress;
    FeatureSupport accelerationStructure;
    FeatureSupport rayQuery;
};

// Owns query data, but borrows handles from the Instance. No device destruction.
// Keep the Instance alive whenever these handles are used.
// Stored Vulkan property structures have null pNext pointers and can be copied.
struct PhysicalDeviceInfo
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice handle = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    std::optional<VkPhysicalDeviceDriverProperties> driver;
    std::vector<VkQueueFamilyProperties> queueFamilies;
    std::vector<VkExtensionProperties> extensions;
    PhysicalDeviceFeatures features;
    std::optional<VkPhysicalDeviceAccelerationStructurePropertiesKHR> accelerationStructureLimits;

    [[nodiscard]] bool hasExtension(const char* name) const;
    [[nodiscard]] bool supportsComputeRayQuery() const;
};

// Snapshot for a specific device/Surface pair; requery before swapchain creation
// or recreation. No presentation support means capabilities is empty.
struct SurfaceSupport
{
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::vector<VkBool32> presentQueues;
    std::optional<VkSurfaceCapabilitiesKHR> capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// Successful queries replace the output; failures leave it unchanged.
// Device enumeration does not require a Surface. Neither query enables features.
[[nodiscard]] bool queryPhysicalDevices(const VulkanInstance& instance, std::vector<PhysicalDeviceInfo>& devices);
[[nodiscard]] bool querySurfaceSupport(const PhysicalDeviceInfo& device, const VulkanSurface& surface, SurfaceSupport& support);

// Print a device and its matching Surface snapshot without querying Vulkan.
void printPhysicalDeviceReport(const PhysicalDeviceInfo& device, const SurfaceSupport& support, std::size_t deviceIndex);
}
