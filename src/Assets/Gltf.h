#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace madcat::assets
{
// CPU-owned geometry. Positions stay mesh-local; instances refer to meshes by
// index rather than duplicating vertices. No parser pointers or GPU layout ABI.
struct MeshPrimitive
{
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
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
};

struct GltfError
{
    std::filesystem::path file;
    std::string message;
};

// glTF 2.0 / GLB, local external buffers or base64 data URIs. Static TRIANGLES,
// FLOAT VEC3 POSITION, optional u8/u16/u32 indices (otherwise generated), matrix
// or TRS nodes. Right-handed, Y up, meters, column vectors; world = parent*local.
// Ignores other vertex attributes, materials, images, cameras/lights and extras.
// Rejects required extensions, compression, sparse accessors, skins, animations,
// morph targets and GPU instancing. This is geometry conversion, not a complete
// glTF conformance validator. Successful results own all data after parser/input
// release. Errors return no partial model; CPU allocation failures may throw.
[[nodiscard]] std::expected<GltfGeometry, GltfError> importGltfGeometry(const std::filesystem::path& file);
}
