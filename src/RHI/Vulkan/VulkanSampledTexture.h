#pragma once
#include "RHI/Texture.h"
#include <vulkan/vulkan.h>

namespace madcat::rhi
{
inline constexpr uint32_t MaterialTextureSet = 0;
inline constexpr uint32_t MaterialDataTextureSet = 3;
inline constexpr uint32_t BaseColorImageBinding = 0;
inline constexpr uint32_t BaseColorSamplerBinding = 1;
inline constexpr uint32_t NormalImageBinding = 2;
inline constexpr uint32_t NormalSamplerBinding = 3;
// Immutable RGBA8 sRGB color or linear UNORM data, full GPU-generated mip chain. Dedicated device memory
// and coherent staging, one startup submission, waits before releasing upload
// storage. Returns in SHADER_READ_ONLY_OPTIMAL, fragment sampled-read state on
// device.queue(); future graph imports must declare this external handoff.
[[nodiscard]] TextureRef createSampledTexture(const VulkanDevice& device, uint32_t width,
    uint32_t height, std::span<const uint8_t> rgba, TextureEncoding encoding = TextureEncoding::Srgb);
[[nodiscard]] TextureBindingRef createTextureBinding(const VulkanDevice& device, TextureRef texture, const SamplerDesc& sampler);
// Fixed set 3 pair. Keeps the source bindings/samplers alive, never updates them.
[[nodiscard]] TextureBindingRef createMaterialDataBinding(const VulkanDevice& device,
    TextureBindingRef metallicRoughness, TextureBindingRef normal);

namespace detail
{
struct TextureBindingAccess
{
    [[nodiscard]] static VkDevice device(const TextureBindingRef& binding);
    [[nodiscard]] static VkDescriptorSetLayout layout(const TextureBindingRef& binding);
    [[nodiscard]] static VkDescriptorSet set(const TextureBindingRef& binding);
    [[nodiscard]] static TextureRef texture(const TextureBindingRef& binding);
    [[nodiscard]] static TextureRef normalTexture(const TextureBindingRef& binding);
};
}
}
