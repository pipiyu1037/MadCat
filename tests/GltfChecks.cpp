#include "Assets/Gltf.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace
{
using madcat::assets::importGltfGeometry;
using Bytes = std::vector<std::byte>;
struct Asset { std::string json; Bytes bytes; };
std::filesystem::path root;

void check(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
void near(float value, float expected) { check(std::isfinite(value) && std::abs(value - expected) < 0.0001f, "Wrong imported coordinate"); }

void append(Bytes& bytes, uint32_t value, size_t count = 4)
{
    for (size_t index = 0; index < count; ++index) { bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 255)); }
}
void appendFloat(Bytes& bytes, float value) { append(bytes, std::bit_cast<uint32_t>(value)); }
void replace(std::string& text, std::string_view from, std::string_view to)
{
    const auto at = text.find(from);
    check(at != std::string::npos, "Fixture replacement did not match");
    text.replace(at, from.size(), to);
}
std::string chars(std::u8string_view value) { return {reinterpret_cast<const char*>(value.data()), value.size()}; }

Asset triangle(unsigned indexType = 5123, bool interleaved = false)
{
    Asset asset;
    for (const std::array<float, 3> point : {std::array{0.0f, 0.0f, 0.0f}, std::array{1.0f, 0.0f, 0.0f}, std::array{0.0f, 1.0f, 0.0f}})
    {
        for (const auto value : point) { appendFloat(asset.bytes, value); }
        if (interleaved) { appendFloat(asset.bytes, 99); }
    }
    const auto positionBytes = asset.bytes.size();
    const auto indexSize = indexType == 5121 ? 1 : indexType == 5123 ? 2 : 4;
    if (indexType != 0) { for (uint32_t index = 0; index < 3; ++index) { append(asset.bytes, index, indexSize); } }
    asset.json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin","byteLength":)" + std::to_string(asset.bytes.size()) +
        R"(}],"bufferViews":[{"buffer":0,"byteLength":)" + std::to_string(positionBytes) + (interleaved ? R"(,"byteStride":16)" : "") + "}";
    if (indexType != 0)
    {
        asset.json += R"(,{"buffer":0,"byteOffset":)" + std::to_string(positionBytes) + R"(,"byteLength":)" + std::to_string(indexSize * 3) + "}";
    }
    asset.json += R"(],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]})";
    if (indexType != 0)
    {
        asset.json += R"(,{"bufferView":1,"componentType":)" + std::to_string(indexType) + R"(,"count":3,"type":"SCALAR"})";
    }
    asset.json += R"(],"meshes":[{"name":"triangle","primitives":[{"attributes":{"POSITION":0})";
    if (indexType != 0) { asset.json += R"(,"indices":1)"; }
    asset.json += R"(}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    return asset;
}

void write(const std::filesystem::path& file, const Bytes& bytes)
{
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(output), "Could not write binary fixture");
}
void write(const std::filesystem::path& file, const std::string& text)
{
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    check(static_cast<bool>(output), "Could not write JSON fixture");
}
std::filesystem::path fixture(std::string_view name, const Asset& asset, bool binary = true)
{
    const auto folder = root / name;
    std::filesystem::create_directories(folder);
    if (binary) { write(folder / "mesh.bin", asset.bytes); }
    const auto file = folder / "model.gltf";
    write(file, asset.json);
    return file;
}
void rejected(std::string_view name, const Asset& asset, std::string_view diagnostic = {})
{
    const auto file = fixture(name, asset);
    const auto result = importGltfGeometry(file);
    check(!result && result.error().file == file && !result.error().message.empty(), "Invalid asset was accepted or lacked diagnostics");
    if (!diagnostic.empty()) { check(result.error().message.find(diagnostic) != std::string::npos, "Unexpected rejection diagnostic"); }
}
void verifyTriangle(const madcat::assets::GltfGeometry& result)
{
    check(result.meshes.size() == 1 && result.meshes[0].primitives.size() == 1, "Wrong imported mesh count");
    const auto& primitive = result.meshes[0].primitives[0];
    check(primitive.positions.size() == 3 && primitive.indices == std::vector<uint32_t>{0, 1, 2}, "Wrong geometry conversion");
    near(primitive.positions[1].x, 1); near(primitive.positions[2].y, 1);
    check(result.nodes.size() == 1 && result.nodes[0].mesh == 0 && result.sceneRoots == std::vector<size_t>{0}, "Wrong mesh instance/root");
}

void unsignedIndicesAndNonindexed()
{
    for (const auto type : {0u, 5121u, 5123u, 5125u})
    {
        const auto file = fixture("index-" + std::to_string(type), triangle(type));
        const auto result = importGltfGeometry(file);
        check(result.has_value(), "Valid index type failed");
        verifyTriangle(*result);
    }
}
void interleavedPositions()
{
    const auto result = importGltfGeometry(fixture("interleaved", triangle(5123, true)));
    check(result.has_value(), "Interleaved POSITION failed");
    verifyTriangle(*result);
}
void glbContainer()
{
    auto asset = triangle();
    replace(asset.json, R"("uri":"mesh.bin",)", "");
    while (asset.json.size() % 4) { asset.json += ' '; }
    while (asset.bytes.size() % 4) { asset.bytes.push_back(std::byte{}); }
    Bytes glb;
    append(glb, 0x46546c67); append(glb, 2);
    append(glb, static_cast<uint32_t>(12 + 8 + asset.json.size() + 8 + asset.bytes.size()));
    append(glb, static_cast<uint32_t>(asset.json.size())); append(glb, 0x4e4f534a);
    for (const auto value : asset.json) { glb.push_back(static_cast<std::byte>(value)); }
    append(glb, static_cast<uint32_t>(asset.bytes.size())); append(glb, 0x004e4942);
    glb.insert(glb.end(), asset.bytes.begin(), asset.bytes.end());
    write(root / "triangle.glb", glb);
    const auto result = importGltfGeometry(root / "triangle.glb");
    check(result.has_value(), "GLB failed");
    verifyTriangle(*result);
    glb.resize(18);
    write(root / "truncated.glb", glb);
    check(!importGltfGeometry(root / "truncated.glb"), "Truncated GLB accepted");
}
std::string base64(const Bytes& bytes)
{
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (size_t index = 0; index < bytes.size(); index += 3)
    {
        const auto count = std::min(size_t{3}, bytes.size() - index);
        uint32_t value = 0;
        for (size_t offset = 0; offset < count; ++offset) { value |= std::to_integer<uint32_t>(bytes[index + offset]) << (16 - offset * 8); }
        result += alphabet[(value >> 18) & 63]; result += alphabet[(value >> 12) & 63];
        result += count > 1 ? alphabet[(value >> 6) & 63] : '=';
        result += count > 2 ? alphabet[value & 63] : '=';
    }
    return result;
}
void dataUri()
{
    auto asset = triangle();
    replace(asset.json, "mesh.bin", "data:application/octet-stream;base64," + base64(asset.bytes));
    const auto result = importGltfGeometry(fixture("embedded", asset, false));
    check(result.has_value(), "Base64 buffer failed");
    verifyTriangle(*result);
}
void unicodePaths()
{
    auto asset = triangle();
    replace(asset.json, "mesh.bin", chars(u8"网格%20数据.bin"));
    const auto folder = root / std::filesystem::path(u8"模型 目录");
    std::filesystem::create_directories(folder);
    const auto file = folder / std::filesystem::path(u8"模型.gltf");
    write(file, asset.json);
    write(folder / std::filesystem::path(u8"网格 数据.bin"), asset.bytes);
    const auto result = importGltfGeometry(file);
    check(result.has_value(), "Unicode/URI-decoded path failed");
    verifyTriangle(*result);
}
void ownedResultsAndRetry()
{
    auto asset = triangle();
    const auto file = fixture("ownership", asset);
    auto result = importGltfGeometry(file);
    check(result.has_value(), "Initial import failed");
    write(file, std::string("broken"));
    write(file.parent_path() / "mesh.bin", Bytes{});
    check(!importGltfGeometry(file), "Broken replacement accepted");
    verifyTriangle(*result);
    write(file, asset.json); write(file.parent_path() / "mesh.bin", asset.bytes);
    auto retry = importGltfGeometry(file);
    check(retry.has_value(), "Import retry failed");
    verifyTriangle(*retry);
}
void sharedMeshesAndHierarchy()
{
    auto asset = triangle();
    replace(asset.json, R"("nodes":[{"mesh":0}])",
        R"("nodes":[{"translation":[10,0,0],"children":[1,2]},{"mesh":0,"translation":[2,0,0]},{"mesh":0,"translation":[-3,0,0]}])");
    const auto result = importGltfGeometry(fixture("hierarchy", asset));
    check(result && result->meshes.size() == 1 && result->nodes.size() == 3 && result->nodes[1].mesh == result->nodes[2].mesh,
        "Mesh instances were duplicated or lost");
    near(result->nodes[1].worldTransform[3].x, 12); near(result->nodes[2].worldTransform[3].x, 7);
    check(result->nodes[1].parent == 0 && result->nodes[0].children == std::vector<size_t>{1, 2}, "Hierarchy links lost");
}
void multiplePrimitives()
{
    auto asset = triangle();
    replace(asset.json, R"("primitives":[{"attributes":{"POSITION":0},"indices":1}])",
        R"("primitives":[{"attributes":{"POSITION":0},"indices":1},{"attributes":{"POSITION":0},"indices":1}])");
    const auto result = importGltfGeometry(fixture("multiple-primitives", asset));
    check(result && result->meshes[0].primitives.size() == 2, "Multiple primitives were lost");
    for (const auto& primitive : result->meshes[0].primitives)
    {
        check(primitive.positions.size() == 3 && primitive.indices == std::vector<uint32_t>{0, 1, 2}, "Primitive geometry changed");
    }
}
void trsMatrixAndShear()
{
    auto asset = triangle();
    replace(asset.json, R"("nodes":[{"mesh":0}])", R"("nodes":[{"scale":[2,1,1],"children":[1]},
        {"mesh":0,"rotation":[0,0,0.7071067811865475,0.7071067811865475],"translation":[1,0,0]},
        {"mesh":0,"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,3,4,5,1]}])");
    const auto result = importGltfGeometry(fixture("transforms", asset));
    check(result.has_value(), "Valid TRS/matrix import failed");
    const auto point = result->nodes[1].worldTransform * glm::vec4(1, 0, 0, 1);
    near(point.x, 2); near(point.y, 1); near(point.z, 0);
    near(result->nodes[2].localTransform[3].x, 3); near(result->nodes[2].worldTransform[3].y, 4);
    asset = triangle();
    replace(asset.json, R"("nodes":[{"mesh":0}])", R"("nodes":[{"mesh":0,"scale":[-1,0,2]}])");
    check(importGltfGeometry(fixture("zero-negative-scale", asset)).has_value(), "Zero/negative scale rejected");
}
void sceneSelection()
{
    auto asset = triangle();
    replace(asset.json, R"("nodes":[{"mesh":0}])", R"("nodes":[{"mesh":0},{"mesh":0}])");
    replace(asset.json, R"("scenes":[{"nodes":[0]}],"scene":0)", R"("scenes":[{"nodes":[0]},{"nodes":[1]}],"scene":1)");
    const auto selected = importGltfGeometry(fixture("selected-scene", asset));
    check(selected && selected->sceneRoots == std::vector<size_t>{1} && selected->nodes.size() == 2, "Default scene selection lost");
    replace(asset.json, R"(,"scene":1)", "");
    const auto first = importGltfGeometry(fixture("first-scene", asset));
    check(first && first->sceneRoots == std::vector<size_t>{0}, "First scene fallback failed");
    replace(asset.json, R"(,"scenes":[{"nodes":[0]},{"nodes":[1]}])", "");
    const auto forest = importGltfGeometry(fixture("root-forest", asset));
    check(forest && forest->sceneRoots == std::vector<size_t>{0, 1}, "Parentless fallback failed");
}
void badFilesAndBuffers()
{
    check(!importGltfGeometry(root / "missing.gltf") && !importGltfGeometry({}), "Missing/empty path accepted");
    auto asset = triangle();
    const auto missing = importGltfGeometry(fixture("missing-buffer", asset, false));
    check(!missing && missing.error().message.find("mesh.bin") != std::string::npos, "Missing buffer lacked path diagnostics");
    asset.bytes.pop_back(); rejected("short-buffer", asset, "truncated");
    asset = triangle(); asset.json = "invalid glTF JSON"; rejected("bad-json", asset);
    asset = triangle(); replace(asset.json, "2.0", "1.0"); rejected("old-version", asset);
    asset = triangle(); replace(asset.json, "mesh.bin", "https://example.invalid/mesh.bin"); rejected("remote-uri", asset, "unsupported URI");
}
void badAccessorsAndIndices()
{
    auto asset = triangle();
    replace(asset.json, R"("byteLength":36)", R"("byteLength":999)"); rejected("view-range", asset, "bufferView");
    asset = triangle(); replace(asset.json, R"("count":3,"type":"VEC3")", R"("count":18446744073709551615,"type":"VEC3")"); rejected("huge-accessor", asset, "accessor");
    asset = triangle(); replace(asset.json, R"("count":3,"type":"VEC3")", R"("byteOffset":2,"count":2,"type":"VEC3")"); rejected("alignment", asset, "misaligned");
    asset = triangle(); replace(asset.json, "VEC3", "VEC2"); rejected("position-type", asset, "POSITION");
    asset = triangle(); asset.bytes[40] = std::byte{3}; rejected("index-range", asset);
    asset = triangle(); asset.bytes[0] = std::byte{0}; asset.bytes[1] = std::byte{0}; asset.bytes[2] = std::byte{0xc0}; asset.bytes[3] = std::byte{0x7f}; rejected("nan-position", asset, "nonfinite POSITION");
    asset = triangle(); replace(asset.json, R"("attributes":{"POSITION":0})", R"("mode":1,"attributes":{"POSITION":0})"); rejected("lines", asset, "TRIANGLES");
    asset = triangle(); replace(asset.json, R"("count":3,"type":"SCALAR")", R"("count":2,"type":"SCALAR")"); rejected("index-count", asset, "multiple of three");
}
void unsupportedData()
{
    auto asset = triangle();
    replace(asset.json, R"("asset":)", R"("extensionsRequired":["KHR_unknown_test"],"asset":)"); rejected("extension", asset, "required extension");
    asset = triangle(); replace(asset.json, R"("asset":)", R"("animations":[{}],"asset":)"); rejected("animation", asset, "animations");
    asset = triangle(); replace(asset.json, R"("bufferView":0,"componentType")",
        R"("sparse":{"count":1,"indices":{"bufferView":1,"componentType":5123},"values":{"bufferView":0}},"bufferView":0,"componentType")"); rejected("sparse", asset, "sparse");
    asset = triangle(); replace(asset.json, R"("attributes":{"POSITION":0})", R"("targets":[{"POSITION":0}],"attributes":{"POSITION":0})"); rejected("morph", asset, "morph");
    asset = triangle(); replace(asset.json, R"("attributes":{"POSITION":0})",
        R"("extensions":{"KHR_draco_mesh_compression":{"bufferView":0,"attributes":{"POSITION":0}}},"attributes":{"POSITION":0})"); rejected("draco", asset, "compression");
    asset = triangle(); replace(asset.json, R"("buffer":0,"byteLength":36)",
        R"("buffer":0,"byteLength":36,"extensions":{"EXT_meshopt_compression":{"buffer":0,"byteOffset":0,"byteLength":36,"byteStride":12,"count":3,"mode":"ATTRIBUTES"}})"); rejected("meshopt", asset, "meshopt");
    asset = triangle(); replace(asset.json, R"("nodes":[{"mesh":0}])",
        R"("nodes":[{"mesh":0,"extensions":{"EXT_mesh_gpu_instancing":{"attributes":{"TRANSLATION":0}}}}])"); rejected("gpu-instancing", asset, "instancing");
    asset = triangle(); replace(asset.json, R"("nodes":[{"mesh":0}])", R"("skins":[{"joints":[0]}],"nodes":[{"mesh":0,"skin":0}])"); rejected("skin", asset, "skins");
}
void invalidHierarchyAndTransforms()
{
    auto asset = triangle();
    replace(asset.json, R"("nodes":[{"mesh":0}])", R"("nodes":[{"children":[1]},{"children":[0],"mesh":0}])"); rejected("cycle", asset);
    asset = triangle(); replace(asset.json, R"("nodes":[{"mesh":0}])", R"("nodes":[{"children":[2]},{"children":[2]},{"mesh":0}])"); rejected("two-parents", asset);
    asset = triangle(); replace(asset.json, R"("nodes":[{"mesh":0}])", R"("nodes":[{"mesh":0,"rotation":[0,0,0,0]}])"); rejected("zero-quaternion", asset, "quaternion");
    asset = triangle(); replace(asset.json, R"("nodes":[{"mesh":0}])", R"("nodes":[{"scale":[1e30,1,1],"children":[1]},{"mesh":0,"scale":[1e30,1,1]}])"); rejected("world-overflow", asset, "overflowing");
    asset = triangle(); replace(asset.json, R"("nodes":[{"mesh":0}])", R"("nodes":[{"mesh":0,"translation":[1e100,0,0]}])"); rejected("nonfinite-trs", asset, "nonfinite");
    asset = triangle(); replace(asset.json, R"("nodes":[{"mesh":0}])", R"("nodes":[{"mesh":0,"translation":[0,0,0],"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}])"); rejected("matrix-and-trs", asset, "both");
}
void deepHierarchy()
{
    auto asset = triangle();
    std::string nodes = R"("nodes":[)";
    constexpr size_t count = 2048;
    for (size_t index = 0; index < count; ++index)
    {
        if (index) { nodes += ','; }
        nodes += R"({"translation":[1,0,0])";
        nodes += index + 1 < count ? R"(,"children":[)" + std::to_string(index + 1) + "]}" : R"(,"mesh":0})";
    }
    nodes += ']';
    replace(asset.json, R"("nodes":[{"mesh":0}])", nodes);
    const auto result = importGltfGeometry(fixture("deep", asset));
    check(result && result->nodes.size() == count, "Deep hierarchy failed");
    near(result->nodes.back().worldTransform[3].x, static_cast<float>(count));
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    if (argc != 2) { std::fprintf(stderr, "Expected fixture output directory\n"); return 1; }
    root = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(root);
    const struct { const char* name; void (*run)(); } cases[] = {
        {"unsigned indices and nonindexed triangles", unsignedIndicesAndNonindexed}, {"interleaved positions", interleavedPositions},
        {"GLB and truncation", glbContainer}, {"base64 data URI", dataUri}, {"Unicode and URI-decoded paths", unicodePaths},
        {"owned results and failure recovery", ownedResultsAndRetry}, {"shared meshes and hierarchy", sharedMeshesAndHierarchy},
        {"multiple mesh primitives", multiplePrimitives},
        {"TRS, matrix and scale conventions", trsMatrixAndShear}, {"default scene and root selection", sceneSelection},
        {"missing/broken files and buffers", badFilesAndBuffers}, {"accessors, index bounds and finite positions", badAccessorsAndIndices},
        {"unsupported features", unsupportedData}, {"invalid hierarchy and transforms", invalidHierarchyAndTransforms},
        {"deep iterative hierarchy", deepHierarchy}
    };
    for (const auto& test : cases)
    {
        try { test.run(); std::printf("Passed: %s\n", test.name); }
        catch (const std::exception& error) { std::fprintf(stderr, "glTF check failed (%s): %s\n", test.name, error.what()); return 1; }
    }
    std::printf("glTF geometry checks passed: %zu cases.\n", std::size(cases));
    return 0;
}
