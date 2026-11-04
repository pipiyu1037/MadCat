#pragma once

#include "Gltf.h"
#include <cgltf.h>

namespace madcat::assets::detail
{
// Private conversion stages. All published data owns its storage; parser and
// decoder handles never escape into the public model representation.
[[nodiscard]] std::expected<void, std::string> importImages(const cgltf_data& data,
    const std::filesystem::path& file, GltfGeometry& output);
[[nodiscard]] std::expected<void, std::string> importMaterials(const cgltf_data& data, GltfGeometry& output);
}
