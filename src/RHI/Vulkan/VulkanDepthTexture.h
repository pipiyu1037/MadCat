#pragma once

#include "RHI/Texture.h"

#include <vulkan/vulkan.h>

namespace madcat::rhi
{
// Query D32_SFLOAT, then D16_UNORM, for optimal depth-attachment usage and the
// requested extent. Owns a dedicated DEVICE_LOCAL allocation and depth-only view.
// Initial layout is UNDEFINED; command recording must transition it explicitly.
// Returns empty on Vulkan/capability failure; CPU allocation failures may throw.
[[nodiscard]] TextureRef createDepthTexture(const VulkanDevice& device, uint32_t width, uint32_t height);

namespace detail
{
struct TextureAccess
{
    [[nodiscard]] static VkDevice device(const TextureRef& texture) noexcept;
    [[nodiscard]] static VkImage image(const TextureRef& texture) noexcept;
    [[nodiscard]] static VkImageView view(const TextureRef& texture) noexcept;
    [[nodiscard]] static VkFormat format(const TextureRef& texture) noexcept;
};
}
}
