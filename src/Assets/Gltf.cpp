#include "Gltf.h"
#include "GltfImport.h"

#include <cgltf.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <span>
#include <string_view>
#include <utility>

namespace madcat::assets
{
namespace
{
struct Failure { std::string message; };
void require(bool condition, std::string_view reason)
{
    if (!condition) { throw Failure{std::string(reason)}; }
}

std::string utf8(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

struct FileContext { std::string lastPath; };
// cgltf passes URI-decoded UTF-8 paths. Use filesystem streams so main files
// and relative buffers work with Unicode Windows paths, too. Never throw into C.
cgltf_result readFile(const cgltf_memory_options*, const cgltf_file_options* options,
    const char* path, cgltf_size* size, void** output)
{
    try
    {
        static_cast<FileContext*>(options->user_data)->lastPath = path;
        const std::filesystem::path filename(std::u8string(reinterpret_cast<const char8_t*>(path)));
        std::ifstream input(filename, std::ios::binary | std::ios::ate);
        if (!input) { return cgltf_result_file_not_found; }
        const auto end = input.tellg();
        if (end < 0) { return cgltf_result_io_error; }
        const auto available = static_cast<uint64_t>(end);
        const auto requested = *size != 0 ? *size : available;
        if (requested > available || requested > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()) ||
            requested > std::numeric_limits<size_t>::max()) { return cgltf_result_io_error; }
        const auto bytes = static_cast<size_t>(requested);
        std::unique_ptr<void, decltype(&std::free)> storage(std::malloc(bytes != 0 ? bytes : 1), &std::free);
        if (!storage) { return cgltf_result_out_of_memory; }
        input.seekg(0);
        if (!input.read(static_cast<char*>(storage.get()), static_cast<std::streamsize>(bytes))) { return cgltf_result_io_error; }
        *size = bytes;
        // Ownership passes to cgltf, which uses our matching release callback.
        *output = storage.release();
        return cgltf_result_success;
    }
    catch (const std::bad_alloc&) { return cgltf_result_out_of_memory; }
    catch (...) { return cgltf_result_io_error; }
}

void releaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }

const char* resultName(cgltf_result result)
{
    switch (result)
    {
    case cgltf_result_success: return "success";
    case cgltf_result_data_too_short: return "data too short";
    case cgltf_result_unknown_format: return "unknown format or unsupported URI";
    case cgltf_result_invalid_json: return "invalid JSON";
    case cgltf_result_invalid_gltf: return "invalid glTF";
    case cgltf_result_file_not_found: return "file not found";
    case cgltf_result_io_error: return "I/O error or truncated buffer";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "legacy glTF is unsupported";
    default: return "parser error";
    }
}

void checkResult(cgltf_result result)
{
    if (result != cgltf_result_success) { throw Failure{resultName(result)}; }
}

glm::mat4 storeMatrix(const glm::dmat4& matrix)
{
    glm::mat4 result(1.0f);
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            const auto value = matrix[column][row];
            require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(), "nonfinite or overflowing node matrix");
            result[column][row] = static_cast<float>(value);
        }
    }
    return result;
}

glm::mat4 localMatrix(const cgltf_node& node)
{
    if (node.has_matrix)
    {
        require(!node.has_translation && !node.has_rotation && !node.has_scale, "matrix and TRS cannot both be present");
        glm::dmat4 matrix(1.0);
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row) { matrix[column][row] = node.matrix[column * 4 + row]; }
        }
        require(matrix[0][3] == 0 && matrix[1][3] == 0 && matrix[2][3] == 0 && matrix[3][3] == 1, "node matrix must be affine");
        return storeMatrix(matrix);
    }
    for (int index = 0; index < 3; ++index)
    {
        require(std::isfinite(node.translation[index]) && std::isfinite(node.scale[index]), "nonfinite node TRS");
    }
    for (const auto value : node.rotation) { require(std::isfinite(value), "nonfinite node rotation"); }
    // glTF stores x/y/z/w; GLM's constructor takes w/x/y/z.
    const glm::dquat rotation(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
    const auto length = glm::length(rotation);
    require(std::abs(length - 1.0) < 0.0001, "node rotation must be a unit quaternion");
    const auto matrix = glm::translate(glm::dmat4(1.0), glm::dvec3(node.translation[0], node.translation[1], node.translation[2])) *
        glm::mat4_cast(rotation / length) * glm::scale(glm::dmat4(1.0), glm::dvec3(node.scale[0], node.scale[1], node.scale[2]));
    return storeMatrix(matrix);
}

struct PrimitiveAttributes
{
    const cgltf_accessor* positions = nullptr;
    const cgltf_accessor* normals = nullptr;
    const cgltf_accessor* texcoords0 = nullptr;
};

PrimitiveAttributes primitiveAttributes(const cgltf_primitive& primitive)
{
    PrimitiveAttributes result;
    for (const auto& attribute : std::span(primitive.attributes, primitive.attributes_count))
    {
        if (attribute.type == cgltf_attribute_type_position)
        {
            require(attribute.index == 0 && !result.positions, "duplicate or invalid POSITION attribute");
            result.positions = attribute.data;
        }
        else if (attribute.type == cgltf_attribute_type_normal)
        {
            require(attribute.index == 0 && !result.normals, "duplicate or invalid NORMAL attribute");
            result.normals = attribute.data;
        }
        else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0)
        {
            require(!result.texcoords0, "duplicate TEXCOORD_0 attribute");
            result.texcoords0 = attribute.data;
        }
    }
    const auto* positions = result.positions;
    require(positions && positions->component_type == cgltf_component_type_r_32f && positions->type == cgltf_type_vec3 &&
        !positions->normalized && positions->count <= UINT32_MAX, "POSITION must be non-normalized FLOAT VEC3 with uint32-sized count");
    if (const auto* normals = result.normals)
    {
        require(normals->component_type == cgltf_component_type_r_32f && normals->type == cgltf_type_vec3 && !normals->normalized,
            "NORMAL must be non-normalized FLOAT VEC3");
        require(normals->count == positions->count, "NORMAL count must match POSITION");
    }
    if (const auto* uv = result.texcoords0)
    {
        const bool floatUv = uv->component_type == cgltf_component_type_r_32f && !uv->normalized;
        const bool integerUv = (uv->component_type == cgltf_component_type_r_8u || uv->component_type == cgltf_component_type_r_16u) && uv->normalized;
        require(uv->type == cgltf_type_vec2 && (floatUv || integerUv), "TEXCOORD_0 must be FLOAT or normalized unsigned byte/short VEC2");
        require(uv->count == positions->count, "TEXCOORD_0 count must match POSITION");
    }
    for (const auto* accessor : {result.positions, result.normals, result.texcoords0})
    {
        if (accessor) { require(accessor->offset % 4 == 0 && accessor->stride % 4 == 0, "vertex attribute requires 4-byte offset and stride alignment"); }
    }
    return result;
}

void preflight(const cgltf_data& data, std::string& context)
{
    require(data.asset.version && std::strcmp(data.asset.version, "2.0") == 0, "only glTF 2.0 is supported");
    require(!data.asset.min_version || std::strcmp(data.asset.min_version, "2.0") == 0, "unsupported minimum glTF version");
    if (data.extensions_required_count != 0) { throw Failure{"unsupported required extension: " + std::string(data.extensions_required[0])}; }
    require(data.skins_count == 0 && data.animations_count == 0, "skins and animations are unsupported");
    require(data.meshes_count != 0, "asset contains no meshes");
    for (size_t index = 0; index < data.buffer_views_count; ++index)
    {
        context = "bufferView " + std::to_string(index);
        const auto& view = data.buffer_views[index];
        require(!view.has_meshopt_compression, "meshopt compression is unsupported");
        require(view.buffer && view.size != 0 && view.offset <= view.buffer->size && view.size <= view.buffer->size - view.offset,
            "bufferView range exceeds buffer");
        require(view.stride == 0 || (view.stride >= 4 && view.stride <= 252 && view.stride % 4 == 0), "invalid bufferView stride");
    }
    // Check sizes without overflowing before cgltf_validate may scan index data.
    for (size_t index = 0; index < data.accessors_count; ++index)
    {
        context = "accessor " + std::to_string(index);
        const auto& accessor = data.accessors[index];
        require(!accessor.is_sparse, "sparse accessors are unsupported");
        require(accessor.buffer_view && accessor.count != 0, "accessor requires a bufferView and nonzero count");
        const auto componentSize = cgltf_component_size(accessor.component_type);
        const auto elementSize = cgltf_calc_size(accessor.type, accessor.component_type);
        require(componentSize != 0 && elementSize != 0 && accessor.stride >= elementSize, "invalid accessor type or stride");
        const auto& view = *accessor.buffer_view;
        require(accessor.offset <= view.size && elementSize <= view.size - accessor.offset &&
            accessor.count - 1 <= (view.size - accessor.offset - elementSize) / accessor.stride, "accessor range exceeds bufferView");
        require(accessor.offset % componentSize == 0 && view.offset % componentSize == 0 && accessor.stride % componentSize == 0,
            "misaligned accessor");
    }
    for (const auto& node : std::span(data.nodes, data.nodes_count)) { require(!node.has_mesh_gpu_instancing, "GPU instancing is unsupported"); }
    for (size_t meshIndex = 0; meshIndex < data.meshes_count; ++meshIndex)
    {
        const auto& mesh = data.meshes[meshIndex];
        require(mesh.weights_count == 0, "morph weights are unsupported");
        for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
        {
            context = "mesh " + std::to_string(meshIndex) + " primitive " + std::to_string(primitiveIndex);
            const auto& primitive = mesh.primitives[primitiveIndex];
            require(!primitive.has_draco_mesh_compression && primitive.targets_count == 0, "compression and morph targets are unsupported");
            // Validate semantics before cgltf_validate's generic count check so
            // diagnostics identify the Primitive and supported attribute.
            (void)primitiveAttributes(primitive);
        }
    }
}
}

std::expected<GltfGeometry, GltfError> importGltfGeometry(const std::filesystem::path& file)
{
    std::string context = "file";
    try
    {
        require(!file.empty(), "empty path");
        FileContext files;
        cgltf_options options{};
        options.file = {readFile, releaseFile, &files};
        const auto filename = utf8(file);
        cgltf_data* parsed = nullptr;
        const auto parseResult = cgltf_parse_file(&options, filename.c_str(), &parsed);
        const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(parsed, &cgltf_free);
        checkResult(parseResult);
        preflight(*data, context);
        context = "buffer loading";
        const auto loadResult = cgltf_load_buffers(&options, data.get(), filename.c_str());
        if (loadResult != cgltf_result_success)
        {
            throw Failure{std::string(resultName(loadResult)) + (files.lastPath.empty() ? "" : ": " + files.lastPath)};
        }
        for (const auto& buffer : std::span(data->buffers, data->buffers_count))
        {
            require(buffer.size != 0 && buffer.data, "missing buffer data");
        }
        context = "glTF validation";
        checkResult(cgltf_validate(data.get()));
        GltfGeometry result;
        context = "images";
        const auto importedImages = detail::importImages(*data, file, result);
        if (!importedImages) { throw Failure{importedImages.error()}; }
        context = "materials";
        const auto importedMaterials = detail::importMaterials(*data, result);
        if (!importedMaterials) { throw Failure{importedMaterials.error()}; }
        result.meshes.resize(data->meshes_count);
        for (size_t meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
        {
            const auto& mesh = data->meshes[meshIndex];
            auto& output = result.meshes[meshIndex];
            output.name = mesh.name ? mesh.name : "";
            require(mesh.primitives_count != 0, "mesh has no primitives");
            for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
            {
                context = "mesh " + std::to_string(meshIndex) + " primitive " + std::to_string(primitiveIndex);
                const auto& primitive = mesh.primitives[primitiveIndex];
                require(primitive.type == cgltf_primitive_type_triangles, "only TRIANGLES primitives are supported");
                const auto attributes = primitiveAttributes(primitive);
                const auto* positions = attributes.positions;
                MeshPrimitive converted;
                if (primitive.material) { converted.material = cgltf_material_index(data.get(), primitive.material); }
                converted.positions.resize(positions->count);
                for (size_t index = 0; index < positions->count; ++index)
                {
                    float value[3]{};
                    require(cgltf_accessor_read_float(positions, index, value, 3) != 0, "cannot read POSITION");
                    require(std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]), "nonfinite POSITION");
                    converted.positions[index] = {value[0], value[1], value[2]};
                }
                if (const auto* normals = attributes.normals)
                {
                    converted.normals.resize(normals->count);
                    for (size_t index = 0; index < normals->count; ++index)
                    {
                        float value[3]{};
                        require(cgltf_accessor_read_float(normals, index, value, 3) != 0, "cannot read NORMAL");
                        require(std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]), "nonfinite NORMAL");
                        const auto length = glm::length(glm::dvec3(value[0], value[1], value[2]));
                        require(std::abs(length - 1.0) < 0.0001, "NORMAL must have unit length within 1e-4");
                        converted.normals[index] = {value[0], value[1], value[2]};
                    }
                }
                if (const auto* uv = attributes.texcoords0)
                {
                    converted.texcoords0.resize(uv->count);
                    for (size_t index = 0; index < uv->count; ++index)
                    {
                        float value[2]{};
                        require(cgltf_accessor_read_float(uv, index, value, 2) != 0, "cannot read TEXCOORD_0");
                        require(std::isfinite(value[0]) && std::isfinite(value[1]), "nonfinite TEXCOORD_0");
                        converted.texcoords0[index] = {value[0], value[1]};
                    }
                }
                const auto* indices = primitive.indices;
                const auto count = indices ? indices->count : positions->count;
                require(count >= 3 && count % 3 == 0 && count <= UINT32_MAX, "triangle index count must be a nonzero multiple of three");
                converted.indices.resize(count);
                if (indices)
                {
                    require(!indices->normalized && cgltf_accessor_unpack_indices(indices, converted.indices.data(), sizeof(uint32_t), count) == count,
                        "cannot read unsigned scalar indices");
                    const auto restart = indices->component_type == cgltf_component_type_r_8u ? UINT8_MAX :
                        indices->component_type == cgltf_component_type_r_16u ? UINT16_MAX : UINT32_MAX;
                    for (const auto index : converted.indices) { require(index < positions->count && index != restart, "index out of range or primitive restart index"); }
                }
                else { std::iota(converted.indices.begin(), converted.indices.end(), uint32_t{0}); }
                output.primitives.push_back(std::move(converted));
            }
        }
        result.nodes.resize(data->nodes_count);
        std::vector<size_t> ordered;
        ordered.reserve(data->nodes_count);
        for (size_t index = 0; index < data->nodes_count; ++index)
        {
            context = "node " + std::to_string(index);
            const auto& node = data->nodes[index];
            auto& output = result.nodes[index];
            output.name = node.name ? node.name : "";
            if (node.parent) { output.parent = cgltf_node_index(data.get(), node.parent); }
            else { ordered.push_back(index); }
            if (node.mesh) { output.mesh = cgltf_mesh_index(data.get(), node.mesh); }
            for (const auto* child : std::span(node.children, node.children_count)) { output.children.push_back(cgltf_node_index(data.get(), child)); }
            output.localTransform = localMatrix(node);
        }
        for (size_t cursor = 0; cursor < ordered.size(); ++cursor)
        {
            const auto index = ordered[cursor];
            context = "node " + std::to_string(index) + " world transform";
            auto& node = result.nodes[index];
            node.worldTransform = node.parent ? storeMatrix(glm::dmat4(result.nodes[*node.parent].worldTransform) * glm::dmat4(node.localTransform)) : node.localTransform;
            ordered.insert(ordered.end(), node.children.begin(), node.children.end());
        }
        require(ordered.size() == result.nodes.size(), "node hierarchy has a cycle");
        const auto* scene = data->scene ? data->scene : data->scenes_count ? &data->scenes[0] : nullptr;
        if (scene)
        {
            std::vector<bool> seen(result.nodes.size());
            for (const auto* root : std::span(scene->nodes, scene->nodes_count))
            {
                const auto index = cgltf_node_index(data.get(), root);
                require(!seen[index], "duplicate scene root");
                seen[index] = true;
                result.sceneRoots.push_back(index);
            }
        }
        else
        {
            for (size_t index = 0; index < result.nodes.size(); ++index) { if (!result.nodes[index].parent) { result.sceneRoots.push_back(index); } }
        }
        return result;
    }
    catch (const Failure& failure) { return std::unexpected(GltfError{file, context + ": " + failure.message}); }
}
}
