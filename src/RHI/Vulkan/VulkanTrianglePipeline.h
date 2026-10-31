#pragma once

#include "RHI/Buffer.h"

#include <vulkan/vulkan.h>

#include <memory>

namespace madcat::rhi
{
class VulkanDevice;
class VulkanShaderModule;

// Fixed location-0 float2 position / location-1 float3 color vertex layout.
struct TriangleVertex
{
    float position[2];
    float color[3];
};

// Backend exercise for triangle.slang: vertex buffer, no descriptors or depth.
// Owns a pipeline and empty layout, retains vertices, and borrows the Device.
// Single-threaded use.
// Finish submitted commands using this pipeline before destroying this owner;
// command buffers that recorded it must not be submitted after destruction.
class VulkanTrianglePipeline
{
public:
    VulkanTrianglePipeline();
    ~VulkanTrianglePipeline();

    VulkanTrianglePipeline(const VulkanTrianglePipeline&) = delete;
    VulkanTrianglePipeline& operator=(const VulkanTrianglePipeline&) = delete;
    VulkanTrianglePipeline(VulkanTrianglePipeline&&) = delete;
    VulkanTrianglePipeline& operator=(VulkanTrianglePipeline&&) = delete;

    // Modules must belong to device and contain vertexMain / fragmentMain with
    // matching stages and location-0 colors. They may be released after creation.
    // colorFormat must be the rendering attachment's supported color format.
    // Failure releases partial resources; repeated initialization preserves state.
    [[nodiscard]] bool initialize(const VulkanDevice& device, VkFormat colorFormat,
        const VulkanShaderModule& vertex, const VulkanShaderModule& fragment, BufferRef vertices);

    [[nodiscard]] VkPipeline handle() const;
    [[nodiscard]] VkDevice deviceHandle() const;
    [[nodiscard]] VkFormat colorFormat() const;
    // The frame context retains a copy until its recorded GPU work completes.
    [[nodiscard]] BufferRef vertexBuffer() const;

    // Caller provides a recording primary graphics command buffer from the same
    // Device, inside compatible dynamic rendering. Extent must fit Device limits.
    // Sets a full-extent viewport/scissor and records Draw(3, 1, 0, 0).
    [[nodiscard]] bool recordDraw(VkCommandBuffer commandBuffer, VkExtent2D extent) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
