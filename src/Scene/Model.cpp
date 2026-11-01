#include "Model.h"

#include <cmath>
#include <string_view>

namespace madcat::scene
{
namespace
{
struct Failure { std::string message; };
void require(bool condition, std::string_view message)
{
    if (!condition) { throw Failure{std::string(message)}; }
}

// Reverse creation order is children-first. Rollback uses leaf deletion, which
// does not allocate, including when a component or parent-link allocation failed.
struct PendingInstance
{
    Scene& scene;
    std::vector<ecs::Entity> created;
    bool committed = false;

    ~PendingInstance()
    {
        if (!committed)
        {
            for (auto entity = created.rbegin(); entity != created.rend(); ++entity) { (void)scene.destroy(*entity); }
        }
    }

    ecs::Entity create()
    {
        const auto entity = scene.create();
        created.push_back(entity); // capacity prepared before any Scene changes
        return entity;
    }
};
}

std::expected<ModelInstance, std::string> instantiateModel(
    Scene& scene, const assets::ModelAssets& assets, assets::ModelAssetId model, ecs::Entity parent)
{
    try
    {
        require(!scene.isIterating(), "cannot instantiate a model during a Scene query");
        require(parent == ecs::Entity{} || scene.isAlive(parent), "invalid instance parent");
        const auto* source = assets.find(model);
        require(source != nullptr, "invalid model asset ID");
        require(!source->sceneRoots.empty(), "selected scene has no nodes");

        // Validate and order the active forest completely before mutating Scene.
        // Mark when enqueuing, so cycles/duplicates cannot grow the work list.
        std::vector<bool> seen(source->nodes.size());
        std::vector<size_t> ordered;
        ordered.reserve(source->nodes.size());
        for (const auto root : source->sceneRoots)
        {
            require(root < source->nodes.size() && !seen[root], "invalid or repeated scene root");
            require(!source->nodes[root].parent, "selected scene root has a parent");
            seen[root] = true;
            ordered.push_back(root);
        }
        for (size_t cursor = 0; cursor < ordered.size(); ++cursor)
        {
            const auto index = ordered[cursor];
            const auto& node = source->nodes[index];
            if (node.mesh) { require(*node.mesh < source->meshes.size(), "invalid active mesh index"); }
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    require(std::isfinite(node.localTransform[column][row]), "nonfinite active local matrix");
                }
            }
            const auto& local = node.localTransform;
            require(local[0][3] == 0 && local[1][3] == 0 && local[2][3] == 0 && local[3][3] == 1,
                "active local matrix must be affine");
            for (const auto child : node.children)
            {
                require(child < source->nodes.size() && !seen[child], "invalid or repeated active child (cycle or multiple parents)");
                require(source->nodes[child].parent == index, "inconsistent active parent/child links");
                seen[child] = true;
                ordered.push_back(child);
            }
        }

        ModelInstance result;
        result.nodes.resize(source->nodes.size());
        PendingInstance pending{scene, {}, false};
        pending.created.reserve(ordered.size() + 1);
        result.root = pending.create();
        require(scene.setParent(result.root, parent), "could not attach instance root");
        for (const auto index : ordered)
        {
            const auto& node = source->nodes[index];
            const auto entity = pending.create();
            result.nodes[index] = entity;
            require(scene.setLocalMatrix(entity, node.localTransform), "could not set node matrix");
            require(scene.setParent(entity, node.parent ? result.nodes[*node.parent] : result.root), "could not attach model node");
            require(scene.add<ModelNode>(entity, model, index) != nullptr, "could not add model node metadata");
            if (node.mesh)
            {
                require(scene.add<MeshInstance>(entity, model, *node.mesh) != nullptr, "could not add mesh reference");
            }
        }
        pending.committed = true;
        return result;
    }
    catch (const Failure& failure) { return std::unexpected(failure.message); }
}
}
