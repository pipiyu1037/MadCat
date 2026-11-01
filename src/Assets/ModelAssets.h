#pragma once

#include "Gltf.h"

#include <limits>
#include <memory>
#include <utility>

namespace madcat::assets
{
// Local to the ModelAssets that issued it. Default/out-of-range IDs are invalid;
// cross-store misuse is not detected. IDs never change or get reused.
struct ModelAssetId
{
    size_t index = std::numeric_limits<size_t>::max();
    friend constexpr bool operator==(ModelAssetId, ModelAssetId) = default;
};

// Startup CPU model storage. Owns immutable converted geometry; no GPU resources.
// Append-only for this increment: IDs and const pointers survive further adds,
// and expire when this store is destroyed. No unloading, replacement or dedup.
// Single-threaded. Add by value copies an lvalue or consumes an rvalue; callers
// must not mutate data through pointers retained across a move into the store.
class ModelAssets
{
public:
    ModelAssets() = default;
    ModelAssets(const ModelAssets&) = delete;
    ModelAssets& operator=(const ModelAssets&) = delete;
    ModelAssets(ModelAssets&&) = delete;
    ModelAssets& operator=(ModelAssets&&) = delete;

    // Accepts CPU-converted models, normally from importGltfGeometry. Allocation
    // failure throws without publishing an ID; no extra geometry validation.
    [[nodiscard]] ModelAssetId add(GltfGeometry model)
    {
        auto owned = std::make_unique<const GltfGeometry>(std::move(model));
        const ModelAssetId id{models_.size()};
        models_.push_back(std::move(owned));
        return id;
    }

    [[nodiscard]] const GltfGeometry* find(ModelAssetId id) const noexcept
    {
        return id.index < models_.size() ? models_[id.index].get() : nullptr;
    }
    [[nodiscard]] size_t size() const noexcept { return models_.size(); }

private:
    std::vector<std::unique_ptr<const GltfGeometry>> models_;
};
}
