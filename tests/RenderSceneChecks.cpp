#include "RenderScene/RenderScene.h"
#include "ModelScene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace
{
using namespace madcat;
std::filesystem::path directory;
void check(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
void near(float value, float expected) { check(std::isfinite(value) && std::abs(value - expected) < .0002f, "Unexpected transformed value"); }
assets::GltfGeometry source()
{
    assets::GltfGeometry model;
    model.meshes.push_back({"two primitives", {
        {{{0,0,0}, {1,0,0}, {0,1,0}}, {0,1,2}},
        {{{0,0,1}, {1,0,1}, {0,1,1}}, {2,1,0}}}});
    model.nodes.resize(2);
    model.nodes[0].mesh = model.nodes[1].mesh = 0;
    model.sceneRoots = {0,1};
    model.nodes[1].localTransform[3].x = 2;
    return model;
}
struct Fixture
{
    assets::ModelAssets assets;
    assets::ModelAssetId id = assets.add(source());
    scene::Scene scene;
    scene::ModelInstance instance = *scene::instantiateModel(scene, assets, id);
    ecs::Entity camera = scene.create();
    Fixture()
    {
        check(scene.add<scene::PerspectiveCamera>(camera) != nullptr, "Camera component failed");
        scene::LocalTransform local; local.translation.z = 3;
        check(scene.setLocalTransform(camera, local) && scene.updateWorldTransforms(), "Camera setup failed");
    }
    std::vector<render::MeshDraw> draws()
    {
        auto result = render::extractDraws(scene, assets, camera, 2);
        check(result.has_value(), "Extraction failed"); return std::move(*result);
    }
};

void extractionAndSharing()
{
    Fixture f;
    auto draws = f.draws();
    check(draws.size() == 4 && draws[0].geometry == draws[2].geometry && draws[1].geometry == draws[3].geometry,
        "Shared Mesh/Primitive keys lost");
    const auto& t = draws[2].transform.clipFromLocal;
    near(t[12], std::sqrt(3.f)); near(t[13], 0); near(t[15], 3);
    auto batch = render::prepareGeometry(f.assets, draws);
    check(batch && batch->ranges.size() == 2 && batch->vertices.size() == 6 && batch->indices.size() == 6, "Instances duplicated geometry");
    check(batch->indices == std::vector<uint32_t>({0,1,2,5,4,3}), "Primitive indices not rebased");
    near(batch->vertices[0].position[0], 0); near(batch->vertices[3].position[2], 1);
    auto resolved = render::resolveDraws(*batch, draws);
    check(resolved && resolved->size() == 4 && (*resolved)[1].firstIndex == 3 && (*resolved)[3].firstIndex == 3, "Primitive ranges lost");
    near((*resolved)[2].transform.clipFromLocal[12], std::sqrt(3.f));
    auto unknown = draws; unknown[0].geometry.primitive = 5;
    check(!render::resolveDraws(*batch, unknown), "Unknown GPU geometry accepted");
}

void editsAndSnapshots()
{
    Fixture f;
    const auto before = f.draws();
    scene::LocalTransform local; local.translation = {1,2,0};
    check(f.scene.setLocalTransform(f.instance.root, local), "Parent edit failed");
    check(!render::extractDraws(f.scene, f.assets, f.camera, 2), "Stale worlds accepted");
    check(f.scene.updateWorldTransforms(), "World update failed");
    const auto after = f.draws();
    near(after[0].transform.clipFromLocal[12], std::sqrt(3.f) / 2);
    near(after[0].transform.clipFromLocal[13], -2 * std::sqrt(3.f));
    near(before[0].transform.clipFromLocal[12], 0);
    check(f.scene.destroySubtree(f.instance.root) && f.scene.updateWorldTransforms(), "Deletion failed");
    check(f.draws().empty(), "Deleted entities remained in extraction");
    near(after[0].transform.clipFromLocal[12], std::sqrt(3.f) / 2);
    check(render::prepareGeometry(f.assets, {}).has_value() && render::resolveDraws({}, {}).has_value(), "Empty Scene rejected");
}

void invalidInputs()
{
    Fixture f;
    check(!render::extractDraws(f.scene, f.assets, {}, 2) && !render::extractDraws(f.scene, f.assets, f.camera, 0), "Invalid camera/aspect accepted");
    f.scene.each<scene::MeshInstance>([&](ecs::Entity, const auto&) {
        check(!render::extractDraws(f.scene, f.assets, f.camera, 2), "Nested extraction accepted");
    });
    auto* mesh = f.scene.tryGet<scene::MeshInstance>(f.instance.nodes[0]);
    mesh->model = {};
    check(!render::extractDraws(f.scene, f.assets, f.camera, 2), "Missing asset accepted");
    mesh->model = f.id; mesh->meshIndex = 100;
    check(!render::extractDraws(f.scene, f.assets, f.camera, 2), "Invalid Mesh accepted");
    mesh->meshIndex = 0;
    scene::LocalTransform local; local.translation.y = std::numeric_limits<float>::max();
    check(f.scene.setLocalTransform(f.instance.root, local) && f.scene.updateWorldTransforms(), "Extreme setup failed");
    check(!render::extractDraws(f.scene, f.assets, f.camera, 2), "Matrix float overflow accepted");
}

void invalidGeometry()
{
    for (int mode = 0; mode < 6; ++mode)
    {
        assets::ModelAssets assets;
        auto model = source();
        auto& primitive = model.meshes[0].primitives[0];
        if (mode == 0) { primitive.positions.clear(); }
        if (mode == 1) { primitive.indices = {0,1}; }
        if (mode == 2) { primitive.indices[0] = 3; }
        if (mode == 3) { primitive.positions[0].x = std::numeric_limits<float>::quiet_NaN(); }
        const auto id = assets.add(std::move(model));
        render::MeshDraw draw{{}, {id, mode == 4 ? size_t(9) : 0, mode == 5 ? size_t(9) : 0}, {}};
        check(!render::prepareGeometry(assets, std::span(&draw, 1)), "Invalid geometry accepted");
    }
}

void assetIdentity()
{
    Fixture f;
    const auto otherId = f.assets.add(source());
    auto second = scene::instantiateModel(f.scene, f.assets, otherId);
    check(second && f.scene.updateWorldTransforms(), "Second asset setup failed");
    auto batch = render::prepareGeometry(f.assets, f.draws());
    check(batch && batch->ranges.size() == 4 && batch->vertices.size() == 12, "Different assets collided in geometry keys");
}

void realModels()
{
    for (const auto* file : {"Suzanne/Suzanne.gltf", "TransformProbe.gltf"})
    {
        auto imported = assets::importGltfGeometry(directory / file);
        check(imported.has_value(), "Reference model import failed");
        assets::ModelAssets assets;
        const auto id = assets.add(std::move(*imported));
        sandbox::ModelScene scene(assets, id);
        auto draws = scene.draws(2);
        check(draws && !draws->empty(), "Reference Scene extraction failed");
        const auto count = draws->size();
        auto batch = render::prepareGeometry(assets, *draws);
        check(batch && batch->ranges.size() * (file[0] == 'S' ? 2 : 4) == count, "Reference geometry sharing failed");
        if (file[0] == 'S') { check(batch->vertices.size() == 11808 && batch->indices.size() == 11808, "Suzanne duplicated on GPU upload path"); }
        check(scene.selectPose(1), "Independent transform failed");
        auto moved = scene.draws(2);
        check(moved && (*draws)[0].transform.clipFromLocal != (*moved)[0].transform.clipFromLocal &&
            (*draws)[count / 2].transform.clipFromLocal == (*moved)[count / 2].transform.clipFromLocal, "First edit affected second instance");
        check(scene.selectPose(7), "Second independent transform failed");
        moved = scene.draws(2);
        check(moved && (*draws)[0].transform.clipFromLocal == (*moved)[0].transform.clipFromLocal &&
            (*draws)[count / 2].transform.clipFromLocal != (*moved)[count / 2].transform.clipFromLocal, "Second edit affected first instance");
        check(scene.selectPose(3) && scene.draws(2)->size() == count / 2, "First instance deletion failed");
        check(scene.selectPose(4) && scene.draws(2)->empty(), "All instance deletion failed");
        check(scene.selectPose(5) && scene.draws(2)->size() == count, "Instance restoration failed");
        check(render::resolveDraws(*batch, *scene.draws(2)).has_value(), "Restoration invalidated uploaded geometry");
        auto restored = scene.draws(2);
        check(scene.selectPose(6) && scene.draws(2)->front().entity == restored->back().entity, "Reverse draw order failed");
    }
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) { throw std::runtime_error("Expected model directory"); }
        directory = argv[1];
        extractionAndSharing(); editsAndSnapshots(); invalidInputs(); invalidGeometry(); assetIdentity(); realModels();
        std::printf("RenderScene checks: 6 groups passed.\n"); return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "RenderScene checks failed: %s\n", error.what()); return 1; }
}
