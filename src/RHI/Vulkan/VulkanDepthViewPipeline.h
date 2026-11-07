#pragma once
#include "RHI/Texture.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace madcat::rhi
{
class VulkanDevice;
class VulkanShaderModule;
// Fixed fullscreen sampled-depth viewer. Device and submitted work outlive it;
// the frame graph owns and retains the image/descriptor until GPU retirement.
class VulkanDepthViewPipeline
{
public:
    VulkanDepthViewPipeline();
    ~VulkanDepthViewPipeline();
    VulkanDepthViewPipeline(const VulkanDepthViewPipeline&) = delete;
    VulkanDepthViewPipeline& operator=(const VulkanDepthViewPipeline&) = delete;
    [[nodiscard]] bool initialize(const VulkanDevice&, VkFormat colorFormat,
        const VulkanShaderModule& vertex, const VulkanShaderModule& fragment);
    [[nodiscard]] bool record(VkCommandBuffer, VkExtent2D, const TextureBindingRef&) const;
    [[nodiscard]] VkDevice deviceHandle() const;
    [[nodiscard]] VkFormat colorFormat() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
