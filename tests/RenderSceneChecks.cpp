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

void attributesAndNormalTransforms()
{
    auto model = source();
    auto& primitive = model.meshes[0].primitives[0];
    primitive.normals.assign(3, glm::normalize(glm::vec3(1)));
    primitive.texcoords0 = {{0,1}, {-.25f,1.5f}, {1,0}};
    assets::ModelAssets assets;
    const auto id = assets.add(std::move(model));
    scene::Scene scene;
    auto instance = scene::instantiateModel(scene, assets, id);
    const auto camera = scene.create();
    check(instance && scene.add<scene::PerspectiveCamera>(camera), "Attribute setup failed");
    scene::LocalTransform cameraPose; cameraPose.translation.z = 3;
    check(scene.setLocalTransform(camera, cameraPose) && scene.updateWorldTransforms(), "Camera setup failed");
    auto draws = render::extractDraws(scene, assets, camera, 2);
    auto batch = render::prepareGeometry(assets, *draws);
    check(batch && batch->vertices.size() == 6 && batch->ranges[0].attributes == 3 && batch->ranges[1].attributes == 0, "Optional attribute flags/sharing lost");
    near(batch->vertices[0].normal[0], 1 / std::sqrt(3.f));
    near(batch->vertices[1].texcoord[0], -.25f); near(batch->vertices[1].texcoord[1], 1.5f);
    check(batch->vertices[3].normal[0] == 0 && batch->vertices[3].texcoord[0] == 0, "Missing attributes were fabricated");
    auto resolved = render::resolveDraws(*batch, *draws);
    check(resolved && (*resolved)[0].transform.attributes == 7 && (*resolved)[1].transform.attributes == 4, "Per-Primitive flags were not resolved");
    for (float xScale : {2.f, -2.f})
    {
        // A(x,y,z) = (xScale*x+y, 3*y, 4*z). Analytic inverse-transpose
        // of (1,1,1) is (1/xScale, (1-1/xScale)/3, 1/4).
        glm::mat4 local(1); local[0][0] = xScale; local[1][0] = 1; local[1][1] = 3; local[2][2] = 4;
        check(scene.setLocalMatrix(instance->root, local) && scene.updateWorldTransforms(), "Shear/reflection setup failed");
        draws = render::extractDraws(scene, assets, camera, 2);
        const auto& packed = draws->front().transform.normalFromLocal;
        glm::vec3 actual{};
        for (int row = 0; row < 3; ++row) { actual[row] = packed[row] + packed[4 + row] + packed[8 + row]; }
        const auto expected = glm::normalize(glm::vec3(1 / xScale, (1 - 1 / xScale) / 3, .25f));
        actual = glm::normalize(actual);
        for (int axis = 0; axis < 3; ++axis) { near(actual[axis], expected[axis]); }
        near(glm::dot(actual, glm::vec3(xScale - 1, -3, 0)), 0);
        near(glm::dot(actual, glm::vec3(xScale, 0, -4)), 0);
        const auto saved = packed;
        cameraPose.rotation = glm::angleAxis(.15f, glm::vec3(0,1,0));
        check(scene.setLocalTransform(camera, cameraPose) && scene.updateWorldTransforms(), "Camera edit failed");
        check(render::extractDraws(scene, assets, camera, 2)->front().transform.normalFromLocal == saved, "World normals depend on camera");
    }
    glm::mat4 singular(1); singular[2][2] = 0;
    check(scene.setLocalMatrix(instance->root, singular) && scene.updateWorldTransforms(), "Singular setup failed");
    draws = render::extractDraws(scene, assets, camera, 2);
    resolved = render::resolveDraws(*batch, *draws);
    check(resolved && (*resolved)[0].transform.attributes == 3 && (*resolved)[0].transform.valid(), "Singular normal transform was used or blocked UV/color");
    scene::LocalTransform tiny; tiny.scale = glm::vec3(1e-30f);
    check(scene.setLocalTransform(instance->root, tiny) && scene.updateWorldTransforms(), "Tiny scale setup failed");
    draws = render::extractDraws(scene, assets, camera, 2);
    check(draws && (draws->front().transform.attributes & rhi::NormalTransformValid), "Invertible tiny scale lost normals");
    near(draws->front().transform.normalFromLocal[0], 1);
}

void invalidAttributes()
{
    for (int mode = 0; mode < 5; ++mode)
    {
        auto model = source();
        auto& primitive = model.meshes[0].primitives[0];
        if (mode == 0) { primitive.normals = {{1,0,0}}; }
        if (mode == 1) { primitive.texcoords0 = {{0,0}}; }
        if (mode == 2) { primitive.normals.assign(3, glm::vec3(0)); }
        if (mode == 3) { primitive.normals.assign(3, glm::vec3(std::numeric_limits<float>::infinity())); }
        if (mode == 4) { primitive.texcoords0.assign(3, glm::vec2(std::numeric_limits<float>::quiet_NaN())); }
        assets::ModelAssets assets;
        const auto id = assets.add(std::move(model));
        const render::MeshDraw draw{{}, {id,0,0}, {}};
        check(!render::prepareGeometry(assets, std::span(&draw, 1)), "Invalid asset attributes reached upload data");
    }
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

void directionalLighting()
{
    scene::Scene scene;
    const auto parent = scene.create(), light = scene.create();
    check(scene.setParent(light, parent), "Light parenting failed");
    auto* component = scene.add<scene::DirectionalLight>(light);
    check(component != nullptr, "Light component failed");
    component->color = {.5f, 1, .25f}; component->irradiance = 2;
    scene::LocalTransform rig, local;
    rig.translation = {10,20,30}; rig.rotation = glm::angleAxis(glm::radians(90.f), glm::vec3(0,1,0));
    local.rotation = glm::angleAxis(glm::radians(-30.f), glm::vec3(1,0,0));
    local.scale = {2,3,4};
    check(scene.setLocalTransform(parent, rig) && scene.setLocalTransform(light, local) && scene.updateWorldTransforms(), "Light rig setup failed");
    const auto snapshot = render::extractLighting(scene);
    check(snapshot && snapshot->valid(), "Light extraction failed");
    near(snapshot->toLight[0], std::sqrt(3.f)/2); near(snapshot->toLight[1], .5f); near(snapshot->toLight[2], 0);
    near(snapshot->irradiance[0], 1); near(snapshot->irradiance[1], 2); near(snapshot->irradiance[2], .5f);
    rig.translation = {-10,-20,-30};
    check(scene.setLocalTransform(parent, rig) && scene.updateWorldTransforms(), "Light translation failed");
    check(render::extractLighting(scene)->toLight == snapshot->toLight, "Translation affected directional light");
    component->irradiance = 0;
    check(render::extractLighting(scene)->irradiance == rhi::FrameLighting{}.irradiance, "Disabled light retained radiance");
    near(snapshot->irradiance[0], 1);
    check(scene.destroy(light) && scene.updateWorldTransforms(), "Light deletion failed");
    const auto empty = render::extractLighting(scene);
    check(empty && empty->valid() && empty->irradiance == rhi::FrameLighting{}.irradiance, "No-light Scene did not produce black lighting");
}

void shadowViews()
{
    rhi::FrameLighting light;
    const auto matrix = render::buildDirectionalShadowMatrix(light);
    check(bool(matrix), "Shadow view failed");
    const auto p = *matrix * glm::vec4(2,2,3.9f,1);
    near(p.x,1); near(p.y,-1); near(p.z,0); near(p.w,1);
    near((*matrix * glm::vec4(0,0,-4,1)).z,1);
    near((*matrix * glm::vec4(0,0,0,1)).z,3.9f/7.9f);
    light.toLight={0,1,0,0};
    const auto vertical = render::buildDirectionalShadowMatrix(light);
    check(bool(vertical), "Vertical light produced degenerate basis");
    near((*vertical * glm::vec4(0,3.9f,0,1)).z,0);
    for (int mode=0; mode<5; ++mode)
    {
        render::DirectionalShadowView view;
        if (mode==0) view.halfExtent.x=0;
        if (mode==1) view.farPlane=view.nearPlane;
        if (mode==2) view.distance=0;
        if (mode==3) view.center.x=std::numeric_limits<float>::infinity();
        if (mode==4) light.toLight={0,0,0,0};
        check(!render::buildDirectionalShadowMatrix(light,view), "Invalid shadow view accepted");
    }
    auto imported=assets::importGltfGeometry(directory/"MaterialProbe.gltf");
    check(bool(imported), "Shadow fixture import failed");
    assets::ModelAssets assets; auto id=assets.add(std::move(*imported)); sandbox::ModelScene scene(assets,id);
    const auto first=scene.shadowDraws(); check(first && !first->empty(), "Shadow extraction failed");
    for (const int pose : {0,2,8})
    {
        check(scene.selectPose(pose),"Shadow camera pose failed");
        const auto frame=scene.lighting(1.5f,2);
        const auto camera=scene.draws(1.5f), shadow=scene.shadowDraws();
        check(frame && frame->valid() && camera && shadow,"Shadow frame extraction failed");
        const auto unpack=[](const auto& data) { glm::mat4 m(0); for(int c=0;c<4;++c)for(int r=0;r<4;++r)m[c][r]=data[c*4+r];return m; };
        const glm::vec4 localPoint(.1f,.2f,0,1);
        const auto actual=unpack(frame->shadowFromClip)*unpack(camera->front().transform.clipFromLocal)*localPoint;
        const auto expected=unpack(shadow->front().transform.clipFromLocal)*localPoint;
        for(int i=0;i<4;++i) { near(actual[i],expected[i]); }
    }
    check(scene.selectPose(0),"Shadow camera reset failed");
    auto invalid=rhi::FrameLighting{}; invalid.shadow[0]=-1;
    check(!invalid.valid(),"Negative shadow bias accepted");
    invalid={}; invalid.shadowFromClip[2]=std::numeric_limits<float>::infinity();
    check(!invalid.valid(),"Nonfinite shadow matrix accepted");
    check(scene.selectPose(2), "Camera edit failed");
    const auto cameraMoved=scene.shadowDraws();
    check(cameraMoved && first->front().transform.clipFromLocal==cameraMoved->front().transform.clipFromLocal, "Shadow follows camera");
    check(scene.selectLighting(1), "Light edit failed");
    const auto rotated=scene.shadowDraws();
    check(rotated && rotated->front().transform.clipFromLocal!=first->front().transform.clipFromLocal, "Shadow ignores light hierarchy");
    check(scene.selectLighting(2), "Light off failed");
    check(scene.shadowDraws()->front().transform.clipFromLocal==first->front().transform.clipFromLocal, "Irradiance changes depth geometry");
    check(scene.selectPose(3) && scene.selectPose(4) && scene.shadowDraws()->empty(), "Empty shadow scene failed");
}

void lightingBoundaries()
{
    scene::Scene scene;
    const auto light = scene.create();
    auto* component = scene.add<scene::DirectionalLight>(light);
    check(component && !render::extractLighting(scene), "Stale light worlds accepted");
    check(scene.updateWorldTransforms(), "Light world update failed");
    scene.each<scene::DirectionalLight>([&](ecs::Entity, const auto&) {
        check(!render::extractLighting(scene), "Lighting extraction inside query accepted");
    });
    component->irradiance = -1;
    check(!render::extractLighting(scene), "Negative light accepted");
    component->irradiance = std::numeric_limits<float>::max(); component->color.x = 2;
    check(!render::extractLighting(scene), "Light float overflow accepted");
    component->color.x = std::numeric_limits<float>::quiet_NaN();
    check(!render::extractLighting(scene), "Nonfinite light accepted");
    *component = {};
    scene::LocalTransform collapsed; collapsed.scale.z = 0;
    check(scene.setLocalTransform(light, collapsed) && scene.updateWorldTransforms() && !render::extractLighting(scene), "Collapsed light axis accepted");
    check(scene.setLocalTransform(light, {}) && scene.updateWorldTransforms(), "Light recovery failed");
    const auto other = scene.create();
    check(scene.add<scene::DirectionalLight>(other) && scene.updateWorldTransforms() && !render::extractLighting(scene), "Multiple lights silently truncated");
    auto invalid = rhi::FrameLighting{}; invalid.toLight[2] = 0;
    check(!invalid.valid(), "Zero GPU light direction accepted");
}

void pbrMaterialAndCamera()
{
    auto cpu=source();
    assets::Material material; material.metallic=.35f; material.roughness=0;
    cpu.materials.push_back(material);
    cpu.meshes[0].primitives[0].material=0;
    assets::ModelAssets assets;
    const auto id=assets.add(cpu);
    const render::MeshDraw requested{{},{id,0,0},{}};
    const auto packed=render::prepareGeometry(assets,std::span(&requested,1));
    check(packed.has_value(),"PBR factors rejected");
    for (const auto& vertex:packed->vertices) {
        near(vertex.metallicRoughness[0],.35f); near(vertex.metallicRoughness[1],0);
    }
    for (float bad : {-1.f,2.f,std::numeric_limits<float>::quiet_NaN()}) {
        cpu.materials[0].roughness=bad;
        assets::ModelAssets invalid;
        const auto badId=invalid.add(cpu);
        const render::MeshDraw badDraw{{},{badId,0,0},{}};
        check(!render::prepareGeometry(invalid,std::span(&badDraw,1)),"Invalid PBR factor accepted");
    }
    Fixture fixture;
    const auto parent=fixture.scene.create();
    scene::LocalTransform pose; pose.translation={.3f,-.4f,1}; pose.scale={2,.5f,1.5f};
    check(fixture.scene.setLocalTransform(parent,pose) && fixture.scene.setParent(fixture.camera,parent) &&
        fixture.scene.updateWorldTransforms(),"PBR camera parent setup failed");
    const auto lighting=render::extractLighting(fixture.scene,fixture.camera,1.7f);
    const auto camera=scene::buildCameraMatrices(fixture.scene,fixture.camera,1.7f);
    check(lighting && camera && lighting->valid(),"PBR camera extraction failed");
    glm::mat4 inverse(0);
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)inverse[c][r]=lighting->worldFromClip[c*4+r];
    const glm::vec4 world(.4f,.2f,-.7f,1);
    const auto restored=inverse*camera->viewProjection*world;
    for(int i=0;i<4;++i)near(restored[i],world[i]);
    const auto position=(*fixture.scene.worldTransform(fixture.camera))[3];
    for(int i=0;i<3;++i)near(lighting->cameraPosition[i],position[i]);
    check(!render::extractLighting(fixture.scene,fixture.camera,0),"Invalid PBR camera aspect accepted");
    check(fixture.scene.setLocalTransform(fixture.camera,{}) &&
        !render::extractLighting(fixture.scene,fixture.camera,1),"Stale PBR camera worlds accepted");
    auto invalid=*lighting; invalid.worldFromClip[0]=std::numeric_limits<float>::infinity();
    check(!invalid.valid(),"Nonfinite inverse camera accepted");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) { throw std::runtime_error("Expected model directory"); }
        directory = argv[1];
        extractionAndSharing(); editsAndSnapshots(); invalidInputs(); invalidGeometry(); assetIdentity(); realModels();
        attributesAndNormalTransforms(); invalidAttributes();
        directionalLighting(); lightingBoundaries(); shadowViews(); pbrMaterialAndCamera();
        std::printf("RenderScene checks: 12 groups passed.\n"); return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "RenderScene checks failed: %s\n", error.what()); return 1; }
}
