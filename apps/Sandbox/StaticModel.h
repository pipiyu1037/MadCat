#pragma once

#include "Assets/Gltf.h"
#include "RHI/ColoredVertex.h"

namespace madcat::sandbox
{
struct StaticModel
{
    std::vector<rhi::ColoredVertex> vertices;
    std::vector<uint32_t> indices;
    size_t nodeInstances = 0;
    size_t primitives = 0;
    glm::dvec3 assetCenter{};
    double previewScale = 1;
};

// Preview bridge for successfully imported geometry. Traverse only sceneRoots,
// bake static node world matrices, duplicate mesh instances, rebase indices.
// Center and uniformly fit the largest AABB dimension to 1.2 scene units; generate
// diagnostic RGB from asset-space AABB coordinates (not glTF material/color).
// Returned vectors own their upload bytes. No ECS/GPU state or GLM ABI upload.
// Invalid/empty active geometry -> error, no partial result; allocation may throw.
[[nodiscard]] std::expected<StaticModel, std::string> prepareStaticModel(const assets::GltfGeometry& geometry);
}
