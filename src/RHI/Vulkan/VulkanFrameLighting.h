#pragma once

#include "RHI/FrameLighting.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace madcat::rhi { class VulkanDevice; }
namespace madcat::rhi::detail
{
// Private one-frame UBO and immutable descriptor. Renderer owns it, waits before
// writes, and retires commands before destruction. Borrows Device and layout.
class VulkanFrameLighting
{
public:
    VulkanFrameLighting();
    ~VulkanFrameLighting();
    VulkanFrameLighting(const VulkanFrameLighting&) = delete;
    VulkanFrameLighting& operator=(const VulkanFrameLighting&) = delete;
    [[nodiscard]] bool initialize(const VulkanDevice& device, VkDescriptorSetLayout layout);
    // Only after this frame's previous submission has completed, before submit.
    void write(const FrameLighting& lighting);
    [[nodiscard]] VkDescriptorSet set() const;
    [[nodiscard]] VkBuffer buffer() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
