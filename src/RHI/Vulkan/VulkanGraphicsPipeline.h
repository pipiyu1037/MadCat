#pragma once
#include "RHI/GraphicsPipeline.h"
#include "RHI/Texture.h"
#include <vulkan/vulkan.h>
#include <span>

namespace madcat::rhi
{
class VulkanDevice;
namespace detail
{
struct GraphicsPipelineAccess
{
    static GraphicsPipelineRef create(const VulkanDevice&, VkFormat presentationFormat, const GraphicsPipelineDesc&);
    static VkDevice device(const GraphicsPipelineRef&);
    static VkFormat format(const GraphicsPipelineRef&);
    static uint32_t constantBytes(const GraphicsPipelineRef&);
    static bool samplesTexture(const GraphicsPipelineRef&);
    static bool bind(const GraphicsPipelineRef&, VkCommandBuffer, VkExtent2D);
    static bool bindTexture(const GraphicsPipelineRef&, VkCommandBuffer, const TextureBindingRef&);
    static bool push(const GraphicsPipelineRef&, VkCommandBuffer, std::span<const std::byte>);
    static bool draw(const GraphicsPipelineRef&, VkCommandBuffer, uint32_t vertexCount);
};
}
}
