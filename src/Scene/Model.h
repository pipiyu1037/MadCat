#pragma once

#include "Assets/ModelAssets.h"
#include "Scene.h"

namespace madcat::scene
{
// Source metadata on every imported node, including hierarchy-only nodes.
// Names and mesh geometry are resolved in ModelAssets, not copied into Entities.
struct ModelNode
{
    assets::ModelAssetId model;
    size_t nodeIndex = 0;
};

// One component per node that references a Mesh. All its Primitives remain in
// the shared CPU asset. This component has no GPU objects or ownership.
struct MeshInstance
{
    assets::ModelAssetId model;
    size_t meshIndex = 0;
};

struct ModelInstance
{
    // Synthetic identity root for independent instance TRS and subtree deletion.
    ecs::Entity root;
    // Indexed by source node index; inactive nodes contain a default Entity.
    std::vector<ecs::Entity> nodes;
};

// Instantiate selected scene roots only, preserving local affine matrices and
// parent/child order. Optional parent must be live in this Scene. Returned handles
// are nonowning: destroySubtree(root) explicitly deletes an instance. Reparented
// nodes follow ordinary Scene rules; the mapping is not kept in sync with edits.
// Asset store must outlive consumers resolving component IDs. Does not update
// world transforms: call updateWorldTransforms after edits, as for other Entities.
// Invalid active hierarchy/mesh references/matrices, missing asset, empty selected
// scene or active query -> error. No Entities remain from a failed operation;
// allocation exceptions propagate after rollback (world cache may be invalidated).
// CPU geometry contents are the importer's responsibility, not revalidated here.
[[nodiscard]] std::expected<ModelInstance, std::string> instantiateModel(
    Scene& scene, const assets::ModelAssets& assets, assets::ModelAssetId model, ecs::Entity parent = {});
}
