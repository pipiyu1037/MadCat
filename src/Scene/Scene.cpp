#include "Scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace madcat::scene
{
namespace
{
bool finite(const glm::vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool finite(const glm::mat4& value) noexcept
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            if (!std::isfinite(value[column][row])) { return false; }
        }
    }
    return true;
}
}

ecs::Entity Scene::create()
{
    const auto entity = registry_.create();
    try { (void)registry_.add<Node>(entity); }
    catch (...)
    {
        (void)registry_.destroy(entity);
        throw;
    }
    worldsCurrent_ = false;
    return entity;
}

void Scene::detach(ecs::Entity entity, Node& node) noexcept
{
    if (auto* old = registry_.tryGet<Node>(node.parent)) { std::erase(old->children, entity); }
    node.parent = {};
}

bool Scene::destroy(ecs::Entity entity) noexcept
{
    if (iterationDepth_ != 0) { return false; }
    auto* node = registry_.tryGet<Node>(entity);
    if (!node || !node->children.empty()) { return false; }
    detach(entity, *node);
    (void)registry_.destroy(entity);
    worldsCurrent_ = false;
    return true;
}

bool Scene::destroySubtree(ecs::Entity entity)
{
    if (iterationDepth_ != 0 || !registry_.isAlive(entity)) { return false; }
    std::vector<ecs::Entity> pending;
    pending.reserve(registry_.size());
    pending.push_back(entity);
    for (size_t index = 0; index < pending.size(); ++index)
    {
        const auto* node = registry_.tryGet<Node>(pending[index]);
        pending.insert(pending.end(), node->children.begin(), node->children.end());
    }
    detach(entity, *registry_.tryGet<Node>(entity));
    for (auto current = pending.rbegin(); current != pending.rend(); ++current)
    {
        (void)registry_.destroy(*current);
    }
    worldsCurrent_ = false;
    return true;
}

bool Scene::setParent(ecs::Entity entity, ecs::Entity parentEntity)
{
    if (iterationDepth_ != 0) { return false; }
    auto* node = registry_.tryGet<Node>(entity);
    const bool root = parentEntity == ecs::Entity{};
    auto* newParent = root ? nullptr : registry_.tryGet<Node>(parentEntity);
    if (!node || (!root && !newParent)) { return false; }
    for (auto ancestor = parentEntity; ancestor != ecs::Entity{}; ancestor = registry_.tryGet<Node>(ancestor)->parent)
    {
        if (ancestor == entity) { return false; }
    }
    if (node->parent == parentEntity) { return true; }
    // Allocate the new link first; erase/Entity assignment cannot throw.
    if (newParent) { newParent->children.push_back(entity); }
    detach(entity, *node);
    node->parent = parentEntity;
    worldsCurrent_ = false;
    return true;
}

ecs::Entity Scene::parent(ecs::Entity entity) const noexcept
{
    const auto* node = registry_.tryGet<Node>(entity);
    return node ? node->parent : ecs::Entity{};
}

std::span<const ecs::Entity> Scene::children(ecs::Entity entity) const noexcept
{
    const auto* node = registry_.tryGet<Node>(entity);
    return node ? std::span<const ecs::Entity>(node->children) : std::span<const ecs::Entity>{};
}

bool Scene::setLocalTransform(ecs::Entity entity, const LocalTransform& local) noexcept
{
    auto* node = registry_.tryGet<Node>(entity);
    if (!node || !finite(local.translation) || !finite(local.scale) ||
        !std::isfinite(local.rotation.w) || !std::isfinite(local.rotation.x) ||
        !std::isfinite(local.rotation.y) || !std::isfinite(local.rotation.z)) { return false; }
    // Double precision avoids overflow/underflow when normalizing finite float
    // quaternions, including very large or subnormal input components.
    const glm::dquat rotation(local.rotation);
    const auto length = glm::length(rotation);
    if (length == 0.0) { return false; }
    auto normalized = local;
    normalized.rotation = glm::quat(rotation / length);
    node->local = normalized;
    worldsCurrent_ = false;
    return true;
}

const LocalTransform* Scene::localTransform(ecs::Entity entity) const noexcept
{
    const auto* node = registry_.tryGet<Node>(entity);
    return node ? &node->local : nullptr;
}

bool Scene::updateWorldTransforms()
{
    if (iterationDepth_ != 0) { return false; }
    std::vector<ecs::Entity> ordered;
    ordered.reserve(registry_.size());
    registry_.each<Node>([&](ecs::Entity entity, const Node& node)
    {
        if (node.parent == ecs::Entity{}) { ordered.push_back(entity); }
    });
    worldsCurrent_ = false;
    for (size_t index = 0; index < ordered.size(); ++index)
    {
        auto* node = registry_.tryGet<Node>(ordered[index]);
        const auto local = glm::translate(glm::mat4(1.0f), node->local.translation) *
            glm::mat4_cast(node->local.rotation) * glm::scale(glm::mat4(1.0f), node->local.scale);
        const auto* parentNode = registry_.tryGet<Node>(node->parent);
        node->world = parentNode ? parentNode->world * local : local;
        if (!finite(node->world)) { return false; }
        ordered.insert(ordered.end(), node->children.begin(), node->children.end());
    }
    worldsCurrent_ = ordered.size() == registry_.size();
    return worldsCurrent_;
}

const glm::mat4* Scene::worldTransform(ecs::Entity entity) const noexcept
{
    if (!worldsCurrent_) { return nullptr; }
    const auto* node = registry_.tryGet<Node>(entity);
    return node ? &node->world : nullptr;
}
}
