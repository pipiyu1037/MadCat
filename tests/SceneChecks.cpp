#include "Scene/Scene.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace
{
using madcat::ecs::Entity;
using madcat::scene::LocalTransform;
using madcat::scene::Scene;
struct Value { int number; };

template<typename T>
concept CanAdd = requires(Scene& scene, Entity entity) { scene.template add<T>(entity); };
static_assert(!CanAdd<LocalTransform> && CanAdd<Value>);
static_assert(!std::is_copy_constructible_v<Scene> && !std::is_move_constructible_v<Scene>);

void check(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void near(float value, float expected)
{
    check(std::abs(value - expected) < 0.0001f, "Unexpected transformed value");
}

void point(const Scene& scene, Entity entity, glm::vec3 input, glm::vec3 expected)
{
    const auto* world = scene.worldTransform(entity);
    check(world != nullptr, "World matrix unavailable");
    const auto result = *world * glm::vec4(input, 1.0f);
    near(result.x, expected.x); near(result.y, expected.y); near(result.z, expected.z); near(result.w, 1.0f);
}

LocalTransform translated(float x, float y = 0.0f, float z = 0.0f)
{
    LocalTransform local;
    local.translation = {x, y, z};
    return local;
}

void identityAndHandles()
{
    Scene scene;
    check(scene.size() == 0 && !scene.isAlive({}) && !scene.localTransform({}), "Empty Scene incorrect");
    check(scene.children({}).empty() && scene.parent({}) == Entity{}, "Invalid hierarchy access incorrect");
    check(scene.updateWorldTransforms() && scene.worldTransformsCurrent(), "Empty update failed");
    const auto entity = scene.create();
    check(scene.isAlive(entity) && scene.size() == 1 && !scene.worldTransform(entity), "Creation state incorrect");
    check(scene.parent(entity) == Entity{} && scene.children(entity).empty(), "New Entity was not root");
    check(scene.updateWorldTransforms(), "Identity update failed");
    point(scene, entity, {1, 2, 3}, {1, 2, 3});
    check(scene.destroy(entity) && !scene.isAlive(entity) && !scene.localTransform(entity), "Destroy failed");
    const auto replacement = scene.create();
    check(replacement.index == entity.index && replacement != entity, "Generation was not changed");
    check(!scene.destroy(entity) && !scene.destroySubtree(entity) && !scene.setLocalTransform(entity, {}),
        "Old Entity changed replacement");
    check(!scene.setParent(replacement, entity), "Old parent accepted");
}

void localTRS()
{
    Scene scene;
    const auto entity = scene.create();
    auto local = translated(10, 20, 30);
    local.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
    local.scale = {2, 3, 4};
    check(scene.setLocalTransform(entity, local) && scene.updateWorldTransforms(), "TRS update failed");
    point(scene, entity, {1, 2, 3}, {4, 22, 42});
    local.scale = {-2, 0, 1};
    check(scene.setLocalTransform(entity, local) && !scene.worldTransform(entity), "Edit did not invalidate worlds");
    check(scene.updateWorldTransforms(), "Negative/zero scale rejected");
    point(scene, entity, {1, 2, 3}, {10, 18, 33});
}

void inheritedTransforms()
{
    Scene scene;
    // Children precede parents in dense storage; update must still order parents first.
    const auto child = scene.create();
    const auto middle = scene.create();
    const auto root = scene.create();
    auto local = translated(10, 0, 0);
    local.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
    check(scene.setLocalTransform(root, local) && scene.setLocalTransform(middle, translated(2)) &&
        scene.setLocalTransform(child, translated(3)), "Local setup failed");
    check(scene.setParent(child, middle) && scene.setParent(middle, root), "Hierarchy setup failed");
    check(scene.updateWorldTransforms(), "Inheritance update failed");
    point(scene, child, {0, 0, 0}, {10, 5, 0});
    local.translation = {20, 0, 0};
    check(scene.setLocalTransform(root, local) && !scene.worldTransform(child), "Parent edit did not invalidate child");
    check(scene.updateWorldTransforms(), "Parent re-update failed");
    point(scene, child, {0, 0, 0}, {20, 5, 0});
}

void shearPreserved()
{
    Scene scene;
    const auto root = scene.create();
    const auto child = scene.create();
    LocalTransform parentLocal;
    parentLocal.scale = {2, 1, 1};
    LocalTransform childLocal;
    childLocal.rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1));
    check(scene.setLocalTransform(root, parentLocal) && scene.setLocalTransform(child, childLocal) &&
        scene.setParent(child, root) && scene.updateWorldTransforms(), "Shear setup failed");
    const auto halfRoot = std::sqrt(0.5f);
    point(scene, child, {1, 0, 0}, {2 * halfRoot, halfRoot, 0});
    point(scene, child, {0, 1, 0}, {-2 * halfRoot, halfRoot, 0});
    const auto& world = *scene.worldTransform(child);
    near(glm::dot(glm::vec3(world[0]), glm::vec3(world[1])), -1.5f);
}

void cycleAndInvalidParentRejection()
{
    Scene scene;
    const auto root = scene.create();
    const auto middle = scene.create();
    const auto child = scene.create();
    check(scene.setParent(middle, root) && scene.setParent(child, middle) && scene.updateWorldTransforms(), "Setup");
    check(!scene.setParent(root, child) && !scene.setParent(middle, child) && !scene.setParent(root, root),
        "Cycle or self parenting accepted");
    check(!scene.setParent(child, {Entity::invalidIndex, 9}) && !scene.setParent({}, root), "Invalid handle accepted");
    check(scene.worldTransformsCurrent() && scene.parent(root) == Entity{} && scene.parent(child) == middle &&
        scene.children(root).size() == 1 && scene.children(middle).size() == 1, "Rejection changed hierarchy");
}

void reparentAndDetach()
{
    Scene scene;
    const auto first = scene.create();
    const auto second = scene.create();
    const auto child = scene.create();
    check(scene.setLocalTransform(first, translated(10)) && scene.setLocalTransform(second, translated(20)) &&
        scene.setLocalTransform(child, translated(3)) && scene.setParent(child, first), "Setup failed");
    check(scene.updateWorldTransforms(), "Initial update failed");
    point(scene, child, {0, 0, 0}, {13, 0, 0});
    check(scene.setParent(child, second) && scene.children(first).empty() && scene.children(second).size() == 1,
        "Reparent did not maintain child links");
    near(scene.localTransform(child)->translation.x, 3);
    check(scene.updateWorldTransforms(), "Reparent update failed");
    point(scene, child, {0, 0, 0}, {23, 0, 0});
    check(scene.setParent(child, second) && scene.children(second).size() == 1 && scene.worldTransformsCurrent(),
        "Repeated parent changed state or duplicated child");
    check(scene.setParent(child, {}) && scene.children(second).empty() && scene.updateWorldTransforms(), "Detach failed");
    point(scene, child, {0, 0, 0}, {3, 0, 0});
}

void deletionPolicies()
{
    int destroyed = 0;
    Scene scene;
    const auto root = scene.create();
    const auto branch = scene.create();
    const auto leaf = scene.create();
    const auto sibling = scene.create();
    check(scene.setParent(branch, root) && scene.setParent(leaf, branch) && scene.setParent(sibling, root), "Setup");
    check(scene.updateWorldTransforms() && !scene.destroy(root) && !scene.destroy(branch), "Parent deletion allowed");
    check(scene.size() == 4 && scene.worldTransformsCurrent(), "Rejected deletion changed state");
    struct Counted { int& count; ~Counted() { ++count; } };
    using Owned = std::unique_ptr<Counted>;
    check(scene.add<Owned>(branch, std::unique_ptr<Counted>(new Counted{destroyed})) &&
        scene.add<Owned>(leaf, std::unique_ptr<Counted>(new Counted{destroyed})), "Owned setup failed");
    check(scene.destroySubtree(branch) && destroyed == 2 && scene.size() == 2, "Subtree cleanup incorrect");
    check(!scene.isAlive(branch) && !scene.isAlive(leaf) && scene.isAlive(root) && scene.isAlive(sibling), "Wrong subtree");
    check(scene.children(root).size() == 1 && scene.children(root)[0] == sibling && scene.parent(sibling) == root,
        "Subtree deletion left dangling links");
    check(scene.destroy(sibling) && scene.children(root).empty() && scene.destroy(root) && scene.size() == 0,
        "Leaf-first deletion failed");
    const auto replacement = scene.create();
    check(scene.parent(replacement) == Entity{} && scene.children(replacement).empty(), "Reused slot inherited hierarchy");
    check(!scene.tryGet<Owned>(replacement) && !scene.setParent(replacement, branch), "Reused slot inherited component/parent");
}

void invalidTransformInputs()
{
    Scene scene;
    const auto entity = scene.create();
    check(scene.updateWorldTransforms(), "Setup failed");
    LocalTransform invalid;
    invalid.translation.x = std::numeric_limits<float>::quiet_NaN();
    check(!scene.setLocalTransform(entity, invalid), "NaN accepted");
    invalid = {};
    invalid.scale.y = std::numeric_limits<float>::infinity();
    check(!scene.setLocalTransform(entity, invalid), "Infinite scale accepted");
    invalid = {};
    invalid.rotation = {0, 0, 0, 0};
    check(!scene.setLocalTransform(entity, invalid), "Zero quaternion accepted");
    invalid.rotation.x = std::numeric_limits<float>::infinity();
    check(!scene.setLocalTransform(entity, invalid) && scene.worldTransformsCurrent(), "Invalid rotation changed state");
    for (const float magnitude : {std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min()})
    {
        LocalTransform local;
        local.rotation = {magnitude, 0, 0, 0};
        check(scene.setLocalTransform(entity, local) && scene.updateWorldTransforms(), "Finite quaternion failed normalization");
        near(scene.localTransform(entity)->rotation.w, 1);
        point(scene, entity, {1, 2, 3}, {1, 2, 3});
    }
}

void overflowRecovery()
{
    Scene scene;
    const auto root = scene.create();
    const auto child = scene.create();
    LocalTransform huge;
    huge.scale = glm::vec3(std::numeric_limits<float>::max());
    LocalTransform doubled;
    doubled.scale = glm::vec3(2);
    check(scene.setLocalTransform(root, huge) && scene.setLocalTransform(child, doubled) && scene.setParent(child, root), "Setup");
    check(!scene.updateWorldTransforms() && !scene.worldTransform(root) && !scene.worldTransform(child),
        "Overflow exposed partial/nonfinite worlds");
    check(scene.setLocalTransform(root, {}) && scene.updateWorldTransforms(), "Overflow recovery failed");
    point(scene, child, {1, 2, 3}, {2, 4, 6});
}

void componentSystemsAndQueryBoundaries()
{
    Scene scene;
    const auto entity = scene.create();
    const auto other = scene.create();
    check(scene.add<Value>(entity, 3) && scene.updateWorldTransforms(), "Setup");
    bool caught = false;
    try
    {
        scene.each<Value>([&](Entity current, Value& value)
        {
            check(!scene.destroy(current) && !scene.destroySubtree(other) && !scene.setParent(current, other) &&
                !scene.updateWorldTransforms() && !scene.add<Value>(other, 1) && !scene.remove<Value>(current),
                "Query allowed structural/update operation");
            try { (void)scene.create(); check(false, "Query allowed creation"); }
            catch (const std::logic_error&) {}
            std::as_const(scene).each<Value>([](Entity, auto& readOnly)
            {
                static_assert(std::is_same_v<decltype(readOnly), const Value&>);
                check(readOnly.number == 3, "Const user component wrong");
            });
            check(!scene.setParent(current, other), "Nested query released outer Scene guard");
            check(scene.setLocalTransform(current, translated(static_cast<float>(value.number))), "System local write failed");
            ++value.number;
            throw std::runtime_error("System callback failure");
        });
    }
    catch (const std::runtime_error&) { caught = true; }
    check(caught && scene.tryGet<Value>(entity)->number == 4 && !scene.worldTransform(entity), "Field edits lost");
    std::as_const(scene).each<Value>([&](Entity current, const Value&)
    {
        check(!scene.setParent(current, other) && !scene.destroy(other) && !scene.updateWorldTransforms(),
            "Const Scene query allowed mutations through mutable alias");
    });
    check(scene.setParent(entity, other) && scene.updateWorldTransforms(), "Query exception left Scene guard active");
    point(scene, entity, {0, 0, 0}, {3, 0, 0});
    check(scene.destroySubtree(other), "Query exception left ECS guard active");
}

void deepHierarchy()
{
    Scene scene;
    auto current = scene.create();
    const auto root = current;
    constexpr int depth = 2048;
    for (int i = 1; i < depth; ++i)
    {
        const auto child = scene.create();
        check(scene.setLocalTransform(child, translated(1)) && scene.setParent(child, current), "Deep setup");
        current = child;
    }
    check(!scene.setParent(root, current) && scene.updateWorldTransforms(), "Deep cycle/update failed");
    point(scene, current, {0, 0, 0}, {static_cast<float>(depth - 1), 0, 0});
    check(scene.destroySubtree(root) && scene.size() == 0 && !scene.isAlive(current), "Deep deletion failed");
}

void hierarchyReferenceModel()
{
    Scene scene;
    constexpr size_t count = 48;
    std::array<Entity, count> entities;
    std::array<int, count> parents;
    std::array<int, count> translations{};
    parents.fill(-1);
    for (auto& entity : entities) { entity = scene.create(); }
    unsigned random = 0x4197ab3u;
    for (int step = 0; step < 512; ++step)
    {
        random = random * 1664525u + 1013904223u;
        const auto index = (random >> 8) % count;
        const auto parent = static_cast<int>((random >> 20) % (count + 1)) - 1;
        bool allowed = true;
        for (int ancestor = parent; ancestor >= 0; ancestor = parents[ancestor])
        {
            if (ancestor == static_cast<int>(index)) { allowed = false; break; }
        }
        check(scene.setParent(entities[index], parent < 0 ? Entity{} : entities[parent]) == allowed,
            "Hierarchy model acceptance mismatch");
        if (allowed) { parents[index] = parent; }
        translations[index] = step % 17 - 8;
        check(scene.setLocalTransform(entities[index], translated(static_cast<float>(translations[index]))) &&
            scene.updateWorldTransforms(), "Hierarchy model update failed");
        for (size_t i = 0; i < count; ++i)
        {
            int expected = 0;
            for (int ancestor = static_cast<int>(i); ancestor >= 0; ancestor = parents[ancestor])
            {
                expected += translations[ancestor];
            }
            point(scene, entities[i], {0, 0, 0}, {static_cast<float>(expected), 0, 0});
            check(scene.parent(entities[i]) == (parents[i] < 0 ? Entity{} : entities[parents[i]]), "Parent model mismatch");
            size_t childCount = 0;
            for (size_t child = 0; child < count; ++child) { if (parents[child] == static_cast<int>(i)) { ++childCount; } }
            check(scene.children(entities[i]).size() == childCount, "Children model count mismatch");
            for (const auto child : scene.children(entities[i]))
            {
                check(child.index < count && child == entities[child.index] && parents[child.index] == static_cast<int>(i),
                    "Children model link mismatch");
            }
        }
    }
}
}

int main()
{
    const struct { const char* name; void (*run)(); } cases[] = {
        {"identity, invalid handles and reuse", identityAndHandles}, {"local TRS conventions", localTRS},
        {"parent-first inheritance and edits", inheritedTransforms}, {"nonuniform scale shear", shearPreserved},
        {"cycle and invalid parent rejection", cycleAndInvalidParentRejection}, {"reparent and detach", reparentAndDetach},
        {"leaf/subtree deletion and resources", deletionPolicies}, {"invalid transforms and quaternion normalization", invalidTransformInputs},
        {"nonfinite composition and recovery", overflowRecovery}, {"component systems and query guards", componentSystemsAndQueryBoundaries},
        {"deep hierarchy without recursion", deepHierarchy}, {"hierarchy reference model", hierarchyReferenceModel}
    };
    size_t passed = 0;
    for (const auto& test : cases)
    {
        try { test.run(); ++passed; std::printf("Passed: %s\n", test.name); }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "Scene check failed (%s): %s\n", test.name, error.what());
            return 1;
        }
    }
    std::printf("Scene checks passed: %zu cases.\n", passed);
    return 0;
}
