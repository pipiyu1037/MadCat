#pragma once

#include <vulkan/vulkan.h>

#include <span>
#include <vector>

namespace madcat::rhi
{
class VulkanDevice;
class VulkanSurface;

// Borrows Device and Surface; both must outlive this owner. Images belong to
// the swapchain and must never be destroyed separately. Before destruction,
// callers must finish all image use and release dependent objects (e.g. views).
// This owner does not wait for GPU work. No copying or moving.
class VulkanSwapchain
{
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&&) = delete;
    VulkanSwapchain& operator=(VulkanSwapchain&&) = delete;

    // Refresh Surface support for the selected physical device before creation.
    // framebufferExtent is in pixels; zero size is rejected (no waiting/rebuild).
    // Failure leaves this owner empty; repeated initialization preserves it.
    [[nodiscard]] bool initialize(const VulkanDevice& device, const VulkanSurface& surface, VkExtent2D framebufferExtent);

    [[nodiscard]] VkSwapchainKHR handle() const { return swapchain_; }
    [[nodiscard]] VkSurfaceFormatKHR surfaceFormat() const { return surfaceFormat_; }
    [[nodiscard]] VkExtent2D extent() const { return extent_; }
    // Borrowed handles; the span and its elements become invalid at destruction.
    [[nodiscard]] std::span<const VkImage> images() const { return images_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surfaceFormat_{};
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    PFN_vkDestroySwapchainKHR destroySwapchain_ = nullptr;
};
}
