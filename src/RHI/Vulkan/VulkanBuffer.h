#pragma once

#include "RHI/Buffer.h"

#include <vulkan/vulkan.h>

namespace madcat::rhi
{
// Backend factory: dedicated HOST_VISIBLE | HOST_COHERENT allocation, initialized
// once before publication. No staging copy or subsequent host writes. Failure
// returns an empty reference and releases partial Vulkan resources; CPU allocation
// failures may throw. Submit reads after this call returns on the same host thread.
[[nodiscard]] BufferRef createVertexBuffer(const VulkanDevice& device, std::span<const std::byte> data);

namespace detail
{
// Native access stays inside the Vulkan backend; ordinary RHI users keep BufferRef.
struct BufferAccess
{
    [[nodiscard]] static VkBuffer handle(const BufferRef& buffer) noexcept;
    [[nodiscard]] static VkDevice device(const BufferRef& buffer) noexcept;
};
}
}
