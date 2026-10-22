#pragma once

#include <array>
#include <memory>

namespace madcat::rhi
{
class VulkanDevice;
class VulkanSwapchain;

enum class ClearFrameResult
{
    Presented,
    Retry,
    SwapchainUnavailable,
    Failed
};

// Minimal Vulkan backend exercise, to be replaced by Pass/Render Graph execution
// as those mechanisms arrive. Borrows Device and Swapchain, which must outlive it.
// Single-threaded use; owns one frame of command and synchronization resources.
class VulkanClearRenderer
{
public:
    VulkanClearRenderer();
    ~VulkanClearRenderer();

    VulkanClearRenderer(const VulkanClearRenderer&) = delete;
    VulkanClearRenderer& operator=(const VulkanClearRenderer&) = delete;
    VulkanClearRenderer(VulkanClearRenderer&&) = delete;
    VulkanClearRenderer& operator=(VulkanClearRenderer&&) = delete;

    // Requires enabled EXT present fences. Failure releases partial resources
    // and permits retry; repeated successful initialization is rejected.
    [[nodiscard]] bool initialize(const VulkanDevice& device, const VulkanSwapchain& swapchain);
    // Linear RGBA. Retry means no image acquired (finite acquire timeout).
    // Unavailable/Failed are terminal: no further acquire or submit until rebuilt.
    [[nodiscard]] ClearFrameResult draw(const std::array<float, 4>& color);
    // Waits for acquisition, command submission and present-resource retirement.
    // Also called on destruction, before releasing views or synchronization.
    [[nodiscard]] bool waitIdle();

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
