#pragma once

#include "RHI/Buffer.h"
#include "RHI/ColoredVertex.h"
#include "RHI/DrawTransform.h"

#include <vulkan/vulkan.h>

#include <memory>

namespace madcat::rhi
{
class VulkanDevice;
class VulkanShaderModule;

// Backend exercise for triangle.slang: indexed triangles, matrix and depth.
// Owns a pipeline and 64-byte vertex Push Constant layout, retains both buffers,
// and borrows the Device.
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
    // Fixed ColoredVertex stride; vertex count fits uint32 and is at least 3.
    // Indices must be same-Device uint32 triangles, nonempty and in vertex bounds.
    // Failure releases partial resources; repeated initialization preserves state.
    [[nodiscard]] bool initialize(const VulkanDevice& device, VkFormat colorFormat,
        const VulkanShaderModule& vertex, const VulkanShaderModule& fragment, BufferRef vertices, BufferRef indices, VkFormat depthFormat);

    [[nodiscard]] VkPipeline handle() const;
    [[nodiscard]] VkDevice deviceHandle() const;
    [[nodiscard]] VkFormat colorFormat() const;
    [[nodiscard]] VkFormat depthFormat() const;
    // The frame context retains a copy until its recorded GPU work completes.
    [[nodiscard]] BufferRef vertexBuffer() const;
    [[nodiscard]] BufferRef indexBuffer() const;
    [[nodiscard]] uint32_t indexCount() const;

    // Caller provides a recording primary graphics command buffer from the same
    // Device, inside compatible dynamic rendering. Extent must fit Device limits.
    // Sets viewport/scissor, binds both buffers, copies a finite matrix and records
    // DrawIndexed(indexCount, 1, 0, 0, 0). Invalid input records no Vulkan commands.
    [[nodiscard]] bool recordDraw(VkCommandBuffer commandBuffer, VkExtent2D extent, const DrawTransform& transform) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
