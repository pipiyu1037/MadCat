#pragma once

#include "RHI/DrawTransform.h"
#include "RHI/IndexedDraw.h"
#include "RHI/Texture.h"

#include <array>
#include <memory>
#include <span>

namespace madcat::rhi
{
class VulkanDevice;
class VulkanSwapchain;
class VulkanTrianglePipeline;

enum class ClearFrameResult
{
    Presented,
    Retry,
    SwapchainUnavailable,
    Failed
};

// Minimal Vulkan backend exercise, to be replaced by Pass/Render Graph execution
// as those mechanisms arrive. Borrows Device, Swapchain and optional triangle
// pipeline, which must outlive it. Retains the matching depth texture and waits
// for GPU completion before releasing
// recorded commands, allowing the caller to destroy the pipeline afterwards.
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
    [[nodiscard]] bool initialize(const VulkanDevice& device, const VulkanSwapchain& swapchain,
        TextureRef depth, const VulkanTrianglePipeline* triangle = nullptr);
    // Clears color and depth (1.0), then draws the supplied matrix snapshots.
    // Retry means no image acquired (finite acquire timeout).
    // Unavailable/Failed are terminal: no further acquire or submit until rebuilt.
    [[nodiscard]] ClearFrameResult draw(const std::array<float, 4>& color, std::span<const DrawTransform> transforms);
    // Draw ranges share the retained buffers; empty input clears the Scene.
    // Range/matrix validation occurs before acquisition. Parameters are copied
    // into commands; no caller-owned snapshots are borrowed after this call.
    [[nodiscard]] ClearFrameResult drawIndexed(const std::array<float, 4>& color, std::span<const IndexedDraw> draws);
    // Waits for acquisition, command submission and present-resource retirement.
    // Also called on destruction, before releasing views or synchronization.
    [[nodiscard]] bool waitIdle();

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
