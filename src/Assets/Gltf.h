#pragma once

#include "Image.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <array>

namespace madcat::assets
{
// CPU-owned geometry. Positions stay mesh-local; instances refer to meshes by
// index rather than duplicating vertices. No parser pointers or GPU layout ABI.
struct MeshPrimitive
{
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    // Empty means absent; otherwise one value per position. Normals remain
    // mesh-local, UV set 0 retains source orientation/range. No generation.
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords0;
    std::optional<size_t> material;
};

enum class AlphaMode { Opaque, Mask, Blend };
// glTF filter/wrap values, independent of Vulkan. Missing filters select linear
// magnification and trilinear minification; missing wrapping selects repeat.
struct Sampler
{
    int magFilter = 9729, minFilter = 9987;
    int wrapS = 10497, wrapT = 10497;
    friend bool operator==(const Sampler&, const Sampler&) = default;
};
struct ModelTexture
{
    size_t image = 0;
    Sampler sampler;
};
struct Material
{
    std::string name;
    std::array<float, 4> baseColor{1,1,1,1};
    std::optional<size_t> baseColorTexture;
    float metallic = 1, roughness = 1;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = .5f;
    bool doubleSided = false;
};

struct ModelMesh
{
    std::string name;
    std::vector<MeshPrimitive> primitives;
};

struct ModelNode
{
    std::string name;
    std::optional<size_t> parent;
    std::vector<size_t> children;
    std::optional<size_t> mesh;
    glm::mat4 localTransform{1.0f};
    glm::mat4 worldTransform{1.0f};
};

struct GltfGeometry
{
    std::vector<ModelMesh> meshes;
    std::vector<ModelNode> nodes;
    // Default scene, otherwise first scene; without scenes, all parentless nodes.
    // Meshes/nodes retain file order, including those outside these active roots.
    std::vector<size_t> sceneRoots;
    std::vector<Material> materials;
    std::vector<Image> images;
    std::vector<ModelTexture> textures;
};

struct GltfError
{
    std::filesystem::path file;
    std::string message;
};

// glTF 2.0 / GLB, local external buffers or base64 data URIs. Static TRIANGLES,
// FLOAT VEC3 POSITION, optional FLOAT VEC3 NORMAL (unit length within 1e-4),
// VEC2 TEXCOORD_0 (FLOAT or normalized u8/u16), u8/u16/u32 indices (otherwise
// generated), matrix or TRS nodes. Attributes must match POSITION count and
// contain finite values; normals are preserved, not renormalized. Missing
// normals/UVs stay empty. Right-handed, Y up, meters, column vectors;
// world = parent*local. Ignores other attributes (including additional UV sets),
// cameras/lights and extras. Imports basic material factors and base-color UV0
// textures, PNG/JPEG images (external URI, base64 or bufferView) and samplers.
// Rejects required extensions, compression, sparse accessors, skins, animations,
// morph targets and GPU instancing. This is static model import, not a complete
// glTF conformance validator. Successful results own all data after parser/input
// release. Errors return no partial model; CPU allocation failures may throw.
[[nodiscard]] std::expected<GltfGeometry, GltfError> importGltfGeometry(const std::filesystem::path& file);
}
