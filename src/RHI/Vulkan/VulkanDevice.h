#pragma once

#include "VulkanPhysicalDevice.h"

#include <span>

namespace madcat::rhi
{
// Matching query snapshots for one device and the target window Surface.
struct PhysicalDeviceCandidate
{
    PhysicalDeviceInfo device;
    SurfaceSupport surfaceSupport;
};

// Borrows the Instance, which must outlive this owner. Queue handles are borrowed
// from the Device and become invalid when it is destroyed. No copying or moving.
// Before destruction, callers must finish GPU use and release owned child objects.
class VulkanDevice
{
public:
    VulkanDevice() = default;
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = delete;
    VulkanDevice& operator=(VulkanDevice&&) = delete;

    // Select and create a Vulkan 1.3 Device with one Graphics/Compute/Present
    // queue. All candidates must belong to the same Instance and Surface.
    // Copies the selected physical device data; Surface support is not retained.
    // Initial creation failure leaves this owner empty; repeated initialization
    // preserves the existing Device.
    [[nodiscard]] bool initialize(std::span<const PhysicalDeviceCandidate> candidates);

    [[nodiscard]] VkDevice handle() const { return device_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const { return physicalDeviceInfo_.handle; }
    // Owned query data, with borrowed Instance/physical device handles. This
    // reference remains valid until Device destruction; empty before initialize.
    [[nodiscard]] const PhysicalDeviceInfo& physicalDeviceInfo() const { return physicalDeviceInfo_; }
    [[nodiscard]] VkQueue queue() const { return queue_; }
    [[nodiscard]] uint32_t queueFamilyIndex() const { return queueFamilyIndex_; }
    [[nodiscard]] bool presentFencesEnabled() const { return presentFencesEnabled_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    PhysicalDeviceInfo physicalDeviceInfo_;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex_ = UINT32_MAX;
    bool presentFencesEnabled_ = false;
    PFN_vkDestroyDevice destroyDevice_ = nullptr;
    PFN_vkDeviceWaitIdle waitIdle_ = nullptr;
};
}
