#include "Scene/Model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

#ifdef _MSC_VER
#include <crtdbg.h>
#endif

// Test-only allocation failure injection; the application allocator is unchanged.
// MSVC iterator-debug vector moves are noexcept but allocate a proxy. Injecting
// failure there terminates inside the STL; exercise all allocation sites in
// Release instead, retaining the full behavior checks in Debug.
#if !defined(_MSC_VER) || _ITERATOR_DEBUG_LEVEL == 0
namespace { std::ptrdiff_t failAfter = -1; }
void* operator new(std::size_t size)
{
    if (failAfter == 0) { throw std::bad_alloc(); }
    if (failAfter > 0) { --failAfter; }
    if (void* pointer = std::malloc(size ? size : 1)) { return pointer; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
#endif

namespace
{
using namespace madcat;
using assets::GltfGeometry;
using assets::ModelAssets;
using scene::Scene;
using scene::instantiateModel;
using ecs::Entity;
std::filesystem::path assetDirectory;
void check(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
void near(float value, float expected) { check(std::isfinite(value) && std::abs(value - expected) < .0001f, "Unexpected matrix/position value"); }
void point(const Scene& scene, Entity entity, glm::vec3 input, glm::vec3 expected)
{
    const auto* matrix = scene.worldTransform(entity);
    check(matrix != nullptr, "World matrix unavailable");
    const auto value = *matrix * glm::vec4(input, 1);
    near(value.x, expected.x); near(value.y, expected.y); near(value.z, expected.z); near(value.w, 1);
}
GltfGeometry model()
{
    GltfGeometry result;
    result.meshes.push_back({"shared triangle", {{{{0,0,0}, {1,0,0}, {0,1,0}}, {2,0,1}}}});
    result.nodes.resize(3);
    result.nodes[0].children = {1, 2};
    result.nodes[1].parent = result.nodes[2].parent = 0;
    result.nodes[1].mesh = result.nodes[2].mesh = 0;
    result.sceneRoots = {0};
    return result;
}
GltfGeometry imported(const char* file)
{
    auto result = assets::importGltfGeometry(assetDirectory / file);
    if (!result) { throw std::runtime_error(result.error().message); }
    return std::move(*result);
}
size_t meshCount(const Scene& scene)
{
    size_t count = 0;
    scene.each<scene::MeshInstance>([&](Entity, const auto&) { ++count; });
    return count;
}

void assetOwnership()
{
    static_assert(!std::is_copy_constructible_v<ModelAssets> && !std::is_move_constructible_v<ModelAssets>);
    static_assert(std::is_same_v<decltype(std::declval<const ModelAssets&>().find({})), const GltfGeometry*>);
    ModelAssets assets;
    check(!assets.find({}) && !assets.find({0}), "Empty/default asset ID resolved");
    auto source = model();
    const auto id = assets.add(source);
    const auto* stored = assets.find(id);
    check(stored && stored->meshes[0].primitives[0].positions.data() != source.meshes[0].primitives[0].positions.data(), "Lvalue asset was not owned");
    source.meshes.clear(); source.nodes.clear();
    for (int i = 0; i < 64; ++i) { check(assets.add(model()) != id, "Asset ID reused"); }
    check(assets.size() == 65 && assets.find(id) == stored, "Adding assets invalidated pointer/ID");
    near(stored->meshes[0].primitives[0].positions[1].x, 1);
    check(!assets.find({assets.size()}), "Out-of-range asset resolved");
}

void probeHierarchy()
{
    ModelAssets assets;
    const auto id = assets.add(imported("TransformProbe.gltf"));
    Scene scene;
    auto instance = instantiateModel(scene, assets, id);
    check(instance && scene.size() == 4 && instance->nodes.size() == 4, "Active Probe node count failed");
    const auto& nodes = instance->nodes;
    check(scene.parent(nodes[0]) == instance->root && scene.parent(nodes[1]) == nodes[0] && scene.parent(nodes[2]) == nodes[0], "Probe hierarchy lost");
    check(nodes[3] == Entity{} && !scene.isAlive(nodes[3]) && meshCount(scene) == 2, "Inactive node instantiated");
    check(scene.children(nodes[0]).size() == 2 && scene.children(nodes[0])[0] == nodes[1] && scene.children(nodes[0])[1] == nodes[2], "Source child order changed");
    check(!scene.tryGet<scene::MeshInstance>(nodes[0]) && scene.localTransform(instance->root), "Hierarchy-only node or root incorrect");
    const auto a = *scene.tryGet<scene::MeshInstance>(nodes[1]), b = *scene.tryGet<scene::MeshInstance>(nodes[2]);
    check(a.model == id && b.model == id && a.meshIndex == 0 && b.meshIndex == 0, "Shared Mesh references changed");
    check(assets.find(id)->meshes[a.meshIndex].primitives.size() == 2, "Multiple Primitives lost");
    scene.each<scene::ModelNode>([&](Entity entity, const auto& node)
    {
        check(node.model == id && nodes[node.nodeIndex] == entity, "Source metadata/mapping failed");
    });
    check(!scene.worldTransformsCurrent() && scene.updateWorldTransforms(), "Instance world update failed");
    point(scene, nodes[1], {-.7f,-.5f,0}, {.5f,-.4f,.9f});
    point(scene, nodes[2], {-.7f,-.5f,0}, {-.62f,-.3f,-.2f});
    auto local = *scene.localMatrix(nodes[0]); local[3][0] += 2;
    check(scene.setLocalMatrix(nodes[0], local) && scene.updateWorldTransforms(), "Imported parent edit failed");
    point(scene, nodes[1], {-.7f,-.5f,0}, {2.5f,-.4f,.9f});
    near(assets.find(id)->nodes[0].localTransform[3].x, .2f);
}

void suzanneInstances()
{
    ModelAssets assets;
    const auto id = assets.add(imported("Suzanne/Suzanne.gltf"));
    const auto* geometry = assets.find(id)->meshes[0].primitives[0].positions.data();
    Scene scene;
    auto first = instantiateModel(scene, assets, id), second = instantiateModel(scene, assets, id);
    check(first && second && first->root != second->root && meshCount(scene) == 2, "Suzanne instances failed");
    scene::LocalTransform local;
    local.translation = {2, 1, 0}; local.scale = {2, 3, 4};
    check(scene.setLocalTransform(first->root, local) && scene.updateWorldTransforms(), "Instance root edit failed");
    point(scene, first->nodes[0], {1, 1, 1}, {4, 4, 4});
    point(scene, second->nodes[0], {1, 1, 1}, {1, 1, 1});
    check(scene.destroySubtree(first->root) && scene.size() == 2 && meshCount(scene) == 1, "Instance deletion affected survivor");
    check(!scene.isAlive(first->nodes[0]) && scene.isAlive(second->nodes[0]) && scene.updateWorldTransforms(), "Instance stale handle/survivor failed");
    point(scene, second->nodes[0], {1, 1, 1}, {1, 1, 1});
    check(assets.find(id)->meshes[0].primitives[0].positions.data() == geometry && assets.find(id)->meshes[0].primitives[0].positions.size() == 11808, "Geometry copied or changed per instance");
    auto third = instantiateModel(scene, assets, id);
    check(third && third->nodes[0] != first->nodes[0] && !scene.isAlive(first->nodes[0]), "Reused Entity revived old handle");
}

void multipleRootsAndParent()
{
    auto source = model();
    source.nodes.resize(4);
    source.nodes[3].children = {0}; source.nodes[0].parent = 3; source.nodes[0].children.clear();
    source.nodes[0].mesh = 0;
    source.nodes[2].parent.reset(); source.nodes[2].children = {1}; source.nodes[1].parent = 2;
    source.sceneRoots = {3, 2};
    source.nodes[3].localTransform[3].x = 2;
    ModelAssets assets;
    const auto id = assets.add(std::move(source));
    Scene scene;
    const auto parent = scene.create();
    scene::LocalTransform local; local.translation.x = 10;
    check(scene.setLocalTransform(parent, local), "Outer parent setup failed");
    const auto instance = instantiateModel(scene, assets, id, parent);
    check(instance && scene.parent(instance->root) == parent && scene.updateWorldTransforms(), "Optional parent failed");
    check(scene.children(instance->root)[0] == instance->nodes[3] && scene.children(instance->root)[1] == instance->nodes[2], "Scene root order changed");
    point(scene, instance->nodes[0], {0, 0, 0}, {12, 0, 0});
    point(scene, instance->nodes[1], {0, 0, 0}, {10, 0, 0});
    check(scene.destroySubtree(instance->root) && scene.isAlive(parent) && scene.children(parent).empty(), "Instance deletion removed outer parent");
}

void affineAndInactiveData()
{
    auto source = model();
    auto& matrix = source.nodes[1].localTransform;
    matrix[0][0] = -2; matrix[1][0] = .5f; matrix[2][2] = 0; matrix[3] = {1, 2, 3, 1};
    // Cached import-time worlds are deliberately wrong: Scene derives its own.
    source.nodes[1].worldTransform[3].x = 900;
    source.nodes.emplace_back();
    source.nodes[3].mesh = 1000;
    source.nodes[3].localTransform[0][0] = std::numeric_limits<float>::infinity();
    ModelAssets assets;
    const auto id = assets.add(std::move(source));
    Scene scene;
    const auto instance = instantiateModel(scene, assets, id);
    check(instance && scene.updateWorldTransforms(), "Affine/inactive asset rejected");
    point(scene, instance->nodes[1], {1, 2, 4}, {0, 4, 3});
    check(!scene.localTransform(instance->nodes[1]) && instance->nodes[3] == Entity{}, "Affine matrix decomposed/inactive node imported");
}

void invalidData()
{
    Scene scene;
    const auto existing = scene.create();
    check(scene.updateWorldTransforms(), "Invalid data setup failed");
    ModelAssets assets;
    auto reject = [&](GltfGeometry source)
    {
        const auto id = assets.add(std::move(source));
        const auto result = instantiateModel(scene, assets, id);
        check(!result && !result.error().empty() && scene.size() == 1 && scene.isAlive(existing) && scene.worldTransformsCurrent(), "Invalid model changed Scene");
    };
    auto source = model(); source.sceneRoots.clear(); reject(source);
    source = model(); source.sceneRoots = {100}; reject(source);
    source = model(); source.sceneRoots = {0, 0}; reject(source);
    source = model(); source.nodes[0].parent = 2; reject(source);
    source = model(); source.nodes[0].children.push_back(1); reject(source);
    source = model(); source.nodes[1].children = {0}; reject(source);
    source = model(); source.nodes[2].children = {1}; reject(source);
    source = model(); source.nodes[0].children = {100}; reject(source);
    source = model(); source.nodes[1].parent = 2; reject(source);
    source = model(); source.nodes[1].mesh = 100; reject(source);
    source = model(); source.nodes[1].localTransform[1][3] = .2f; reject(source);
    source = model(); source.nodes[1].localTransform[0][0] = std::numeric_limits<float>::quiet_NaN(); reject(source);
    check(!instantiateModel(scene, assets, {}) && !instantiateModel(scene, assets, {assets.size()}), "Invalid asset ID accepted");
    const auto valid = assets.add(model());
    const auto dead = scene.create(); check(scene.destroy(dead), "Stale parent setup failed");
    check(!instantiateModel(scene, assets, valid, dead), "Stale parent accepted");
    check(instantiateModel(scene, assets, valid).has_value(), "Instantiation did not recover from errors");
}

void queryBoundaries()
{
    Scene scene;
    ModelAssets assets;
    const auto id = assets.add(model());
    const auto instance = instantiateModel(scene, assets, id);
    check(instance.has_value(), "Query setup failed");
    const auto count = scene.size();
    std::as_const(scene).each<scene::ModelNode>([&](Entity, const auto&)
    {
        check(!instantiateModel(scene, assets, id) && scene.size() == count, "Instantiation modified active query");
    });
    try { scene.each<scene::ModelNode>([](Entity, auto&) { throw std::runtime_error("query exit"); }); }
    catch (const std::runtime_error&) {}
    check(!scene.isIterating() && instantiateModel(scene, assets, id).has_value(), "Query exception leaked guard");
}

void deepHierarchy()
{
    auto source = model();
    constexpr size_t count = 2048;
    source.nodes.clear(); source.nodes.resize(count);
    for (size_t i = 0; i < count; ++i)
    {
        source.nodes[i].localTransform[3].x = 1;
        if (i) { source.nodes[i].parent = i - 1; }
        if (i + 1 < count) { source.nodes[i].children = {i + 1}; }
    }
    source.nodes.back().mesh = 0;
    ModelAssets assets;
    const auto id = assets.add(std::move(source));
    Scene scene;
    const auto instance = instantiateModel(scene, assets, id);
    check(instance && scene.size() == count + 1 && scene.updateWorldTransforms(), "Deep instantiation failed");
    point(scene, instance->nodes.back(), {0, 0, 0}, {static_cast<float>(count), 0, 0});
    check(scene.destroySubtree(instance->root) && scene.size() == 0, "Deep instance deletion failed");
}

#if !defined(_MSC_VER) || _ITERATOR_DEBUG_LEVEL == 0
void allocationRollback()
{
    ModelAssets assets;
    const auto id = assets.add(model());
    bool failedAfterCreation = false;
    for (std::ptrdiff_t offset = 0; offset < 256; ++offset)
    {
        Scene scene;
        const auto parent = scene.create(), child = scene.create();
        check(scene.setParent(child, parent) && scene.updateWorldTransforms(), "Allocation setup failed");
        bool failed = false;
        failAfter = offset;
        try
        {
            auto result = instantiateModel(scene, assets, id, parent);
            failAfter = -1;
            check(result.has_value(), "Allocation test returned an ordinary error");
        }
        catch (const std::bad_alloc&) { failAfter = -1; failed = true; }
        catch (...) { failAfter = -1; throw; }
        if (!failed)
        {
            check(failedAfterCreation, "Allocation injection never exercised mutated Scene rollback");
            std::printf("Allocation rollback checked %td failing allocation positions.\n", offset);
            return;
        }
        failedAfterCreation = failedAfterCreation || !scene.worldTransformsCurrent();
        check(scene.size() == 2 && scene.isAlive(parent) && scene.isAlive(child) && scene.parent(child) == parent &&
            scene.children(parent).size() == 1 && scene.children(parent)[0] == child && meshCount(scene) == 0,
            "Allocation failure left partial nodes or changed old hierarchy");
        size_t nodes = 0; scene.each<scene::ModelNode>([&](Entity, const auto&) { ++nodes; });
        check(nodes == 0 && scene.updateWorldTransforms(), "Rollback left components or invalid hierarchy");
        point(scene, child, {1, 2, 3}, {1, 2, 3});
        check(instantiateModel(scene, assets, id, parent).has_value(), "Retry after allocation failure failed");
    }
    check(false, "Allocation injection did not reach a successful instantiation");
}
#endif
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    if (argc != 2) { std::fprintf(stderr, "Expected model asset directory\n"); return 1; }
    assetDirectory = std::filesystem::absolute(argv[1]);
    const struct { const char* name; void (*run)(); } cases[] = {
        {"immutable model ownership and stable IDs", assetOwnership}, {"Probe active hierarchy, matrices and shared Mesh", probeHierarchy},
        {"two Suzanne instances, independent edits and deletion", suzanneInstances}, {"multiple roots and outer parent", multipleRootsAndParent},
        {"affine local matrices, cached worlds and inactive isolation", affineAndInactiveData}, {"invalid data and recovery", invalidData},
        {"query boundaries and exception recovery", queryBoundaries}, {"deep nonrecursive instantiation and deletion", deepHierarchy},
#if !defined(_MSC_VER) || _ITERATOR_DEBUG_LEVEL == 0
        {"allocation failure rollback and retry", allocationRollback}
#endif
    };
    for (const auto& test : cases)
    {
        try { test.run(); std::printf("Passed: %s\n", test.name); }
        catch (const std::exception& error) { std::fprintf(stderr, "Model Scene check failed (%s): %s\n", test.name, error.what()); return 1; }
    }
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL != 0
    std::printf("Skipped allocation failure injection under MSVC iterator debugging; run Release for rollback probes.\n");
#endif
    std::printf("Model Scene checks passed: %zu cases.\n", std::size(cases));
    return 0;
}
