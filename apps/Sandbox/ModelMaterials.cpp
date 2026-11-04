#include "ModelMaterials.h"

#include <algorithm>

namespace madcat::sandbox
{
std::expected<std::vector<rhi::TextureBindingRef>, std::string> prepareMaterialBindings(
    const rhi::VulkanDevice& device, const assets::ModelAssets& assets, std::span<const render::MaterialData> materials)
{
    const uint8_t white[]{255,255,255,255};
    auto fallbackImage = rhi::createSampledTexture(device,1,1,white);
    auto fallback = rhi::createTextureBinding(device,fallbackImage,{});
    if (!fallback) { return std::unexpected("White fallback texture/binding creation failed"); }
    struct Image { assets::ModelAssetId model; size_t index; rhi::TextureRef texture; };
    struct Binding { assets::ModelAssetId model; size_t index; rhi::TextureBindingRef binding; };
    std::vector<Image> images;
    std::vector<Binding> bindings;
    std::vector<rhi::TextureBindingRef> result;
    const auto wrap = [](int value) { return value == 33071 ? rhi::SamplerDesc::Wrap::Clamp : value == 33648 ? rhi::SamplerDesc::Wrap::Mirror : rhi::SamplerDesc::Wrap::Repeat; };
    for (const auto& material : materials)
    {
        if (!material.material.baseColorTexture) { result.push_back(fallback); continue; }
        const auto textureIndex = *material.material.baseColorTexture;
        const auto existing = std::ranges::find_if(bindings,[&](const auto& value) { return value.model == material.model && value.index == textureIndex; });
        if (existing != bindings.end()) { result.push_back(existing->binding); continue; }
        const auto* model = assets.find(material.model);
        if (!model || textureIndex >= model->textures.size()) { return std::unexpected("Material references an invalid CPU texture"); }
        const auto& texture = model->textures[textureIndex];
        auto image = std::ranges::find_if(images,[&](const auto& value) { return value.model == material.model && value.index == texture.image; });
        if (image == images.end())
        {
            if (texture.image >= model->images.size()) { return std::unexpected("Texture references an invalid CPU image"); }
            const auto& cpu = model->images[texture.image];
            auto uploaded = rhi::createSampledTexture(device,cpu.width,cpu.height,cpu.rgba);
            if (!uploaded) { return std::unexpected("Base-color image upload failed"); }
            images.push_back({material.model,texture.image,std::move(uploaded)}); image = images.end()-1;
        }
        const auto& source = texture.sampler;
        rhi::SamplerDesc sampler;
        sampler.linearMag = source.magFilter == 9729;
        sampler.linearMin = source.minFilter == 9729 || source.minFilter == 9985 || source.minFilter == 9987;
        sampler.mips = source.minFilter == 9728 || source.minFilter == 9729 ? rhi::SamplerDesc::Mips::None :
            source.minFilter == 9986 || source.minFilter == 9987 ? rhi::SamplerDesc::Mips::Linear : rhi::SamplerDesc::Mips::Nearest;
        sampler.wrapU = wrap(source.wrapS); sampler.wrapV = wrap(source.wrapT);
        auto binding = rhi::createTextureBinding(device,image->texture,sampler);
        if (!binding) { return std::unexpected("Base-color sampler/descriptor creation failed"); }
        bindings.push_back({material.model,textureIndex,binding}); result.push_back(std::move(binding));
    }
    if (result.empty()) { result.push_back(std::move(fallback)); }
    return result;
}
}
