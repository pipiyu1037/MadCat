#include "StaticModel.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
using madcat::assets::GltfGeometry;
using madcat::sandbox::prepareStaticModel;
std::filesystem::path fixturePath;
void check(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
void near(double value, double expected) { check(std::isfinite(value) && std::abs(value - expected) < 0.00001, "Wrong transformed/packed value"); }

GltfGeometry triangle()
{
    GltfGeometry model;
    model.meshes.push_back({"triangle", {{{{-1,-1,0}, {1,-1,.5f}, {0,1,0}}, {2,0,1}}}});
    model.nodes.resize(1);
    model.nodes[0].mesh = 0;
    model.sceneRoots = {0};
    return model;
}

void actualFixture()
{
    auto imported = madcat::assets::importGltfGeometry(fixturePath);
    check(static_cast<bool>(imported), "Repository fixture did not import");
    auto prepared = prepareStaticModel(*imported);
    check(prepared && prepared->nodeInstances == 2 && prepared->primitives == 4 && prepared->vertices.size() == 12,
        "Active scene, shared mesh or multiple primitives lost");
    const std::vector<uint32_t> expectedIndices{2,0,1, 4,5,3, 8,6,7, 10,11,9};
    check(prepared->indices == expectedIndices, "Primitive/instance indices were not rebased");
    // Independent, analytically transformed vertices for 90-degree Y rotation,
    // parent/child translations and the second instance's nonuniform scale.
    const double world[][3] = {
        {.5,-.4,.9}, {.5,-.4,-.5}, {.8,.8,.2}, {0,-.1,.6}, {0,-.1,-.3}, {1,.7,.1},
        {-.62,-.3,-.2}, {.22,-.3,-.2}, {-.2,.66,.01}, {-.44,-.06,-.55}, {.1,-.06,-.55}, {-.14,.58,.15}
    };
    const double minimum[]{-.62,-.4,-.55}, extent[]{1.62,1.2,1.45}, center[]{.19,.2,.175};
    near(prepared->previewScale, 20.0 / 27);
    for (int axis = 0; axis < 3; ++axis) { near(prepared->assetCenter[axis], center[axis]); }
    for (size_t index = 0; index < std::size(world); ++index)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            near(prepared->vertices[index].position[axis], (world[index][axis] - center[axis]) * 20 / 27);
            near(prepared->vertices[index].color[axis], .2 + .6 * (world[index][axis] - minimum[axis]) / extent[axis]);
        }
    }
    imported = GltfGeometry{};
    near(prepared->vertices[0].position[2], (.9 - .175) * 20 / 27);
}

void ownedAndPacked()
{
    auto source = triangle();
    auto output = prepareStaticModel(source);
    check(output && output->indices == std::vector<uint32_t>{2,0,1}, "Triangle conversion failed");
    source.meshes.clear();
    source.nodes.clear();
    near(output->vertices[0].position[0], -.6);
    near(output->vertices[1].position[2], .15);
    near(output->vertices[0].color[0], .2);
    check(sizeof(madcat::rhi::ColoredVertex) == 44, "Unexpected upload stride");
}

void activeOnly()
{
    auto source = triangle();
    source.nodes.resize(2);
    source.nodes[1].mesh = 999; // Inactive data must not affect preview bounds.
    check(prepareStaticModel(source).has_value(), "Inactive node was rendered");
    source.sceneRoots.clear();
    check(!prepareStaticModel(source), "Empty selected scene displayed unrelated geometry");
    source.sceneRoots = {0};
    source.nodes[0].mesh.reset();
    check(!prepareStaticModel(source), "Scene without geometry accepted");
}

void invalidHierarchy()
{
    auto source = triangle();
    source.sceneRoots = {3};
    check(!prepareStaticModel(source), "Invalid scene root accepted");
    source.sceneRoots = {0,0};
    check(!prepareStaticModel(source), "Duplicate root accepted");
    source.sceneRoots = {0};
    source.nodes[0].children = {0};
    check(!prepareStaticModel(source), "Cyclic active tree accepted");
    source.nodes[0].children = {1};
    check(!prepareStaticModel(source), "Invalid child accepted");
    source.nodes[0].children.clear();
    source.nodes[0].mesh = 1;
    check(!prepareStaticModel(source), "Invalid mesh accepted");
}

void invalidGeometry()
{
    auto source = triangle();
    auto& primitive = source.meshes[0].primitives[0];
    primitive.indices = {0,1,3};
    check(!prepareStaticModel(source), "Out-of-range index accepted");
    primitive.indices = {0,1};
    check(!prepareStaticModel(source), "Nontriangle count accepted");
    primitive.indices.clear();
    check(!prepareStaticModel(source), "Missing indices accepted");
    primitive.indices = {0,1,2};
    primitive.positions.assign(3, {1,1,1});
    check(!prepareStaticModel(source), "Zero extent accepted");
    primitive.positions.clear();
    check(!prepareStaticModel(source), "Missing positions accepted");
    source.meshes[0].primitives.clear();
    check(!prepareStaticModel(source), "Mesh without primitives accepted");
}

void matrices()
{
    auto source = triangle();
    source.nodes[0].worldTransform[0][0] = -2;
    source.nodes[0].worldTransform[1][0] = .5f; // Shear retained; no TRS decomposition.
    auto output = prepareStaticModel(source);
    check(output.has_value(), "Negative scale/shear failed");
    source.nodes[0].worldTransform[0][3] = 1;
    check(!prepareStaticModel(source), "Nonaffine matrix accepted");
    source.nodes[0].worldTransform[0][3] = 0;
    source.nodes[0].worldTransform[2][2] = std::numeric_limits<float>::infinity();
    check(!prepareStaticModel(source), "Nonfinite matrix accepted");
    source = triangle();
    source.meshes[0].primitives[0].positions[0].x = std::numeric_limits<float>::quiet_NaN();
    check(!prepareStaticModel(source), "Nonfinite position accepted");
}

void extremeFits()
{
    for (const auto scale : {std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min()})
    {
        auto source = triangle();
        source.nodes[0].worldTransform[0][0] = scale;
        source.nodes[0].worldTransform[1][1] = scale;
        source.nodes[0].worldTransform[2][2] = scale;
        const auto output = prepareStaticModel(source);
        check(output.has_value(), "Double preview fitting failed for extreme input");
        near(output->vertices[0].position[0], -.6);
        near(output->vertices[1].position[2], .15);
    }
    auto flat = triangle();
    for (auto& point : flat.meshes[0].primitives[0].positions) { point.z = 0; }
    const auto output = prepareStaticModel(flat);
    check(output.has_value(), "Flat geometry rejected");
    near(output->vertices[0].color[2], .5);
}

void deepHierarchy()
{
    auto source = triangle();
    source.nodes.resize(2048);
    source.nodes[0].mesh.reset();
    for (size_t index = 1; index < source.nodes.size(); ++index) { source.nodes[index - 1].children = {index}; }
    source.nodes.back().mesh = 0;
    check(prepareStaticModel(source).has_value(), "Deep active hierarchy failed");
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    try
    {
        check(argc == 2, "Expected repository fixture path");
        fixturePath = argv[1];
        const std::pair<const char*, void(*)()> cases[] = {
            {"repository glTF, transforms, shared meshes and index rebasing", actualFixture},
            {"owned upload layout", ownedAndPacked}, {"selected active scene", activeOnly},
            {"invalid hierarchy", invalidHierarchy}, {"invalid geometry", invalidGeometry},
            {"matrices and finite values", matrices}, {"extreme and flat fitting", extremeFits},
            {"deep iterative traversal", deepHierarchy}
        };
        for (const auto& [name, run] : cases) { run(); std::printf("Passed: %s\n", name); }
        std::printf("Static model checks passed: %zu cases.\n", std::size(cases));
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "Static model checks failed: %s\n", error.what()); return 1; }
}
