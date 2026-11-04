#include "GltfImport.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace madcat::assets::detail
{
namespace
{
struct Failure { std::string message; };
void require(bool valid, std::string_view why) { if (!valid) { throw Failure{std::string(why)}; } }
}

std::expected<void, std::string> importMaterials(const cgltf_data& data, GltfGeometry& output)
{
    std::string context;
    try
    {
        for (size_t index = 0; index < data.textures_count; ++index)
        {
            context = "texture " + std::to_string(index);
            const auto& source = data.textures[index];
            require(source.image, "texture has no supported core image");
            ModelTexture texture{cgltf_image_index(&data, source.image), {}};
            if (source.sampler)
            {
                const auto& sampler = *source.sampler;
                if (sampler.mag_filter) { texture.sampler.magFilter = sampler.mag_filter; }
                if (sampler.min_filter) { texture.sampler.minFilter = sampler.min_filter; }
                texture.sampler.wrapS = sampler.wrap_s; texture.sampler.wrapT = sampler.wrap_t;
            }
            const auto& s = texture.sampler;
            require(s.magFilter == 9728 || s.magFilter == 9729, "invalid magnification filter");
            require(s.minFilter == 9728 || s.minFilter == 9729 || (s.minFilter >= 9984 && s.minFilter <= 9987), "invalid minification filter");
            for (const auto wrap : {s.wrapS, s.wrapT}) { require(wrap == 10497 || wrap == 33071 || wrap == 33648, "invalid sampler wrapping"); }
            output.textures.push_back(texture);
        }
        for (size_t index = 0; index < data.materials_count; ++index)
        {
            context = "material " + std::to_string(index);
            const auto& source = data.materials[index];
            Material material;
            material.name = source.name ? source.name : "";
            if (source.has_pbr_metallic_roughness)
            {
                const auto& pbr = source.pbr_metallic_roughness;
                std::copy_n(pbr.base_color_factor, 4, material.baseColor.begin());
                material.metallic = pbr.metallic_factor; material.roughness = pbr.roughness_factor;
                const auto& texture = pbr.base_color_texture;
                if (texture.texture)
                {
                    require(texture.texcoord == 0 && !texture.has_transform, "base color requires TEXCOORD_0 without texture transforms");
                    material.baseColorTexture = cgltf_texture_index(&data, texture.texture);
                }
            }
            for (const auto factor : material.baseColor) { require(std::isfinite(factor) && factor >= 0 && factor <= 1, "invalid base color factor"); }
            require(std::isfinite(material.metallic) && material.metallic >= 0 && material.metallic <= 1 &&
                std::isfinite(material.roughness) && material.roughness >= 0 && material.roughness <= 1, "invalid metallic/roughness factor");
            material.alphaMode = source.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask : source.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
            material.alphaCutoff = source.alpha_cutoff;
            require(std::isfinite(material.alphaCutoff) && material.alphaCutoff >= 0, "invalid alpha cutoff");
            material.doubleSided = source.double_sided != 0;
            output.materials.push_back(std::move(material));
        }
        return {};
    }
    catch (const Failure& failure) { return std::unexpected(context + ": " + failure.message); }
}
}
