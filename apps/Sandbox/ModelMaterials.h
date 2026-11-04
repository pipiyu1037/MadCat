#pragma once
#include "RenderScene/RenderScene.h"
#include "RHI/Vulkan/VulkanSampledTexture.h"

namespace madcat::sandbox
{
// Startup bridge: CPU asset IDs -> immutable RHI bindings. One white fallback,
// one upload per referenced model/image; equal texture keys share bindings.
[[nodiscard]] std::expected<std::vector<rhi::TextureBindingRef>, std::string> prepareMaterialBindings(
    const rhi::VulkanDevice& device, const assets::ModelAssets& assets, std::span<const render::MaterialData> materials);
}
