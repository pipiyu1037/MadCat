#pragma once

#include "RHI/Buffer.h"
#include "RHI/ColoredVertex.h"
#include "RHI/DrawTransform.h"
#include "RHI/IndexedDraw.h"
#include "RHI/Texture.h"
#include <span>

#include <vulkan/vulkan.h>

#include <memory>

namespace madcat::rhi
{
class VulkanDevice;
class VulkanShaderModule;

// Backend exercise for triangle.slang: indexed triangles, matrix and depth.
// Owns a pipeline and 128-byte vertex Push Constant layout, retains both buffers,
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
    // triangle.slang requires material bindings, including a white fallback.
    // An empty span is reserved for descriptor-free shader exercises; it cannot
    // record Material/Lit view. Nonempty entries share the fixed set-0 image/sampler
    // layout and create the set-1 fragment lighting layout. Each renderer owns
    // its own lighting buffer/set; geometry/materials are retained until completion.
    // Failure releases partial resources; repeated initialization preserves state.
    [[nodiscard]] bool initialize(const VulkanDevice& device, VkFormat colorFormat,
        const VulkanShaderModule& vertex, const VulkanShaderModule& fragment, BufferRef vertices, BufferRef indices, VkFormat depthFormat,
        std::span<const TextureBindingRef> materials = {});

    [[nodiscard]] VkPipeline handle() const;
    [[nodiscard]] VkDevice deviceHandle() const;
    [[nodiscard]] VkFormat colorFormat() const;
    [[nodiscard]] VkFormat depthFormat() const;
    // The frame context retains a copy until its recorded GPU work completes.
    [[nodiscard]] BufferRef vertexBuffer() const;
    [[nodiscard]] BufferRef indexBuffer() const;
    [[nodiscard]] uint32_t indexCount() const;
    [[nodiscard]] std::span<const TextureBindingRef> materialBindings() const;
    // Backend-only layout borrowed by each renderer's independent frame UBO.
    [[nodiscard]] VkDescriptorSetLayout lightingLayout() const;

    // Caller provides a recording primary graphics command buffer from the same
    // Device, inside compatible dynamic rendering. Extent must fit Device limits.
    // Sets viewport/scissor, binds both buffers, copies valid draw constants and records
    // DrawIndexed(indexCount, 1, 0, 0, 0). Invalid input records no Vulkan commands.
    // With materials, lighting must be a same-Device set allocated from
    // lightingLayout(), backed by data retained unchanged until GPU completion.
    [[nodiscard]] bool recordDraw(VkCommandBuffer commandBuffer, VkExtent2D extent, const DrawTransform& transform,
        VkDescriptorSet lighting = VK_NULL_HANDLE) const;
    // Nonempty triangle range into the retained index buffer. Validated before
    // recording; instances share buffers and supply their own matrix snapshots.
    [[nodiscard]] bool recordIndexedDraw(VkCommandBuffer commandBuffer, VkExtent2D extent, const IndexedDraw& draw,
        VkDescriptorSet lighting = VK_NULL_HANDLE) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
}
