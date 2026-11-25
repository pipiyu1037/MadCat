#include "VulkanMaterials.h"

#include <algorithm>

namespace madcat::render
{
std::expected<std::vector<rhi::MaterialBinding>, std::string> prepareMaterialBindings(
    const rhi::VulkanDevice& device, const assets::ModelAssets& assets, std::span<const render::MaterialData> materials)
{
    const uint8_t white[]{255,255,255,255};
    auto fallbackImage = rhi::createSampledTexture(device,1,1,white);
    auto fallback = rhi::createTextureBinding(device,fallbackImage,{});
    auto fallbackData = rhi::createSampledTexture(device,1,1,white,rhi::TextureEncoding::Linear);
    auto fallbackMr = rhi::createTextureBinding(device,fallbackData,{});
    const uint8_t flatNormal[]{128,128,255,255};
    auto fallbackNormal = rhi::createTextureBinding(device,rhi::createSampledTexture(device,1,1,flatNormal,rhi::TextureEncoding::Linear),{});
    if (!fallback || !fallbackMr || !fallbackNormal) { return std::unexpected("Material fallback texture/binding creation failed"); }
    struct Image { assets::ModelAssetId model; size_t index; rhi::TextureEncoding encoding; rhi::TextureRef texture; };
    struct Binding { assets::ModelAssetId model; size_t index; rhi::TextureEncoding encoding; rhi::TextureBindingRef binding; };
    std::vector<Image> images;
    std::vector<Binding> bindings;
    std::vector<rhi::MaterialBinding> result;
    const auto wrap = [](int value) { return value == 33071 ? rhi::SamplerDesc::Wrap::Clamp : value == 33648 ? rhi::SamplerDesc::Wrap::Mirror : rhi::SamplerDesc::Wrap::Repeat; };
    const auto prepare = [&](assets::ModelAssetId modelId, std::optional<size_t> textureSource, rhi::TextureEncoding encoding)
        -> std::expected<rhi::TextureBindingRef,std::string> {
        if (!textureSource) { return encoding == rhi::TextureEncoding::Srgb ? fallback : fallbackMr; }
        const auto textureIndex = *textureSource;
        const auto existing = std::ranges::find_if(bindings,[&](const auto& value) { return value.model == modelId && value.index == textureIndex && value.encoding == encoding; });
        if (existing != bindings.end()) { return existing->binding; }
        const auto* model = assets.find(modelId);
        if (!model || textureIndex >= model->textures.size()) { return std::unexpected("Material references an invalid CPU texture"); }
        const auto& texture = model->textures[textureIndex];
        auto image = std::ranges::find_if(images,[&](const auto& value) { return value.model == modelId && value.index == texture.image && value.encoding == encoding; });
        if (image == images.end())
        {
            if (texture.image >= model->images.size()) { return std::unexpected("Texture references an invalid CPU image"); }
            const auto& cpu = model->images[texture.image];
            auto uploaded = rhi::createSampledTexture(device,cpu.width,cpu.height,cpu.rgba,encoding);
            if (!uploaded) { return std::unexpected("Material image upload failed"); }
            images.push_back({modelId,texture.image,encoding,std::move(uploaded)}); image = images.end()-1;
        }
        const auto& source = texture.sampler;
        rhi::SamplerDesc sampler;
        sampler.linearMag = source.magFilter == 9729;
        sampler.linearMin = source.minFilter == 9729 || source.minFilter == 9985 || source.minFilter == 9987;
        sampler.mips = source.minFilter == 9728 || source.minFilter == 9729 ? rhi::SamplerDesc::Mips::None :
            source.minFilter == 9986 || source.minFilter == 9987 ? rhi::SamplerDesc::Mips::Linear : rhi::SamplerDesc::Mips::Nearest;
        sampler.wrapU = wrap(source.wrapS); sampler.wrapV = wrap(source.wrapT);
        auto binding = rhi::createTextureBinding(device,image->texture,sampler);
        if (!binding) { return std::unexpected("Material sampler/descriptor creation failed"); }
        bindings.push_back({modelId,textureIndex,encoding,binding}); return binding;
    };
    for (const auto& material : materials)
    {
        auto base = prepare(material.model,material.material.baseColorTexture,rhi::TextureEncoding::Srgb);
        if (!base) { return std::unexpected(base.error()); }
        auto mr = prepare(material.model,material.material.metallicRoughnessTexture,rhi::TextureEncoding::Linear);
        if (!mr) { return std::unexpected(mr.error()); }
        auto normal = material.material.normalTexture ? prepare(material.model,material.material.normalTexture,rhi::TextureEncoding::Linear) :
            std::expected<rhi::TextureBindingRef,std::string>(fallbackNormal);
        if (!normal) { return std::unexpected(normal.error()); }
        auto data = rhi::createMaterialDataBinding(device,std::move(*mr),std::move(*normal));
        if (!data) { return std::unexpected("Material MR/normal binding creation failed"); }
        result.push_back({std::move(*base),std::move(data)});
    }
    if (result.empty()) {
        auto data = rhi::createMaterialDataBinding(device,std::move(fallbackMr),std::move(fallbackNormal));
        if (!data) { return std::unexpected("Fallback MR/normal binding creation failed"); }
        result.push_back({std::move(fallback),std::move(data)});
    }
    return result;
}
}
