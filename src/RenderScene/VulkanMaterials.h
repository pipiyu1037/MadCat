#pragma once
#include "RenderScene/RenderScene.h"
#include "RHI/Vulkan/VulkanSampledTexture.h"

namespace madcat::render
{
// Startup bridge: CPU asset IDs -> immutable RHI bindings. White fallbacks for
// each encoding plus a flat normal; one upload per model/image/encoding, and equal texture/encoding
// keys share source bindings. Fixed set3 pairs MR/normal and retains both sources.
// Color and data uses of one CPU image never share mips.
[[nodiscard]] std::expected<std::vector<rhi::MaterialBinding>, std::string> prepareMaterialBindings(
    const rhi::VulkanDevice& device, const assets::ModelAssets& assets, std::span<const render::MaterialData> materials);
}
