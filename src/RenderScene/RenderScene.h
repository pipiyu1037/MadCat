#pragma once

#include "Scene/Camera.h"
#include "Scene/Model.h"
#include "RHI/ColoredVertex.h"
#include "RHI/IndexedDraw.h"

namespace madcat::render
{
struct PrimitiveId
{
    assets::ModelAssetId model;
    size_t mesh = 0;
    size_t primitive = 0;
    friend bool operator==(PrimitiveId, PrimitiveId) = default;
};

struct MeshDraw
{
    ecs::Entity entity;
    PrimitiveId geometry;
    rhi::DrawTransform transform;
};

// CPU value snapshots, no Scene/component pointers, GPU objects or Vulkan types.
// Caller updates world transforms first and selects a camera explicitly.
// Rejects active queries, stale worlds, invalid camera/asset/Mesh or matrix
// overflow. Empty renderable Scene succeeds. Failure publishes no partial list;
// allocation exceptions propagate. IDs remain local to the supplied owners.
// World normals use inverse-transpose of the world's linear part, rescaled by
// one positive factor for float storage. Singular worlds clear the normal-valid
// flag, leaving color/UV draws available; fragment visualization marks magenta.
[[nodiscard]] std::expected<std::vector<MeshDraw>, std::string> extractDraws(
    const scene::Scene& scene, const assets::ModelAssets& assets, ecs::Entity camera, float aspect);

struct PrimitiveRange
{
    PrimitiveId geometry;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t attributes = 0;
    uint32_t material = 0;
};

struct MaterialData
{
    assets::ModelAssetId model;
    std::optional<size_t> source;
    assets::Material material;
};

struct GeometryBatch
{
    std::vector<rhi::ColoredVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<PrimitiveRange> ranges;
    std::vector<MaterialData> materials;
};

// Packs each referenced Primitive once, in Mesh local coordinates. Instances do
// not duplicate vertices. Diagnostic colors use each Primitive's local AABB.
// Copies optional local normals/UV0 with per-Primitive presence flags; absent
// values have zero-filled storage. CPU GLM objects are never the upload ABI.
// Indices are rebased into the shared vertex buffer. Validates geometry before
// publication, including the current Device's guaranteed 24-bit index range.
// Rebuild/upload explicitly when introducing geometry not in this startup batch.
// Deduplicates material IDs and packs base factors/alpha/culling per draw.
// BLEND rendering and textured primitives without UV0 are rejected explicitly.
[[nodiscard]] std::expected<GeometryBatch, std::string> prepareGeometry(
    const assets::ModelAssets& assets, std::span<const MeshDraw> draws);

// Resolves keys against an immutable batch; empty draws succeeds, unknown keys
// fail without partial output. Result owns all per-frame command parameter bytes.
[[nodiscard]] std::expected<std::vector<rhi::IndexedDraw>, std::string> resolveDraws(
    const GeometryBatch& batch, std::span<const MeshDraw> draws);
}
