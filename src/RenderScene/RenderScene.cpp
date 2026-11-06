#include "RenderScene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace madcat::render
{
namespace
{
struct Failure { std::string message; };
void require(bool condition, std::string_view message)
{
    if (!condition) { throw Failure{std::string(message)}; }
}
}

std::expected<rhi::FrameLighting, std::string> extractLighting(const scene::Scene& scene)
{
    try
    {
        require(!scene.isIterating() && scene.worldTransformsCurrent(), "Lighting requires updated worlds outside queries");
        rhi::FrameLighting result;
        size_t count = 0;
        scene.each<scene::DirectionalLight>([&](ecs::Entity entity, const auto& light) {
            require(++count == 1, "Only one directional light is currently supported");
            const auto* world = scene.worldTransform(entity);
            require(world != nullptr, "Light world transform is unavailable");
            const glm::dvec3 axis((*world)[2]);
            const auto length = glm::length(axis);
            require(std::isfinite(length) && length > 0, "Directional light world +Z axis is collapsed");
            require(std::isfinite(light.irradiance) && light.irradiance >= 0, "Directional light irradiance must be finite and nonnegative");
            for (size_t index = 0; index < 3; ++index)
            {
                const auto channel = light.color[static_cast<int>(index)];
                const auto value = double(channel) * light.irradiance;
                require(std::isfinite(channel) && channel >= 0 && value <= std::numeric_limits<float>::max(), "Directional light color/irradiance is invalid or exceeds float range");
                result.toLight[index] = static_cast<float>(axis[static_cast<int>(index)] / length);
                result.irradiance[index] = static_cast<float>(value);
            }
        });
        return result;
    }
    catch (const Failure& failure) { return std::unexpected(failure.message); }
}

std::expected<std::vector<MeshDraw>, std::string> extractDraws(
    const scene::Scene& scene, const assets::ModelAssets& assets, ecs::Entity camera, float aspect)
{
    try
    {
        require(!scene.isIterating() && scene.worldTransformsCurrent(), "RenderScene requires updated worlds outside queries");
        const auto view = scene::buildCameraMatrices(scene, camera, aspect);
        require(view.has_value(), "RenderScene camera is invalid");
        std::vector<MeshDraw> result;
        scene.each<scene::MeshInstance>([&](ecs::Entity entity, const auto& instance) {
            const auto* model = assets.find(instance.model);
            require(model && instance.meshIndex < model->meshes.size(), "RenderScene has an invalid asset or Mesh reference");
            const auto* world = scene.worldTransform(entity);
            require(world != nullptr, "RenderScene world matrix is unavailable");
            const auto matrix = glm::dmat4(view->viewProjection) * glm::dmat4(*world);
            rhi::DrawTransform transform{};
            transform.normalFromLocal.fill(0);
            transform.attributes = 0;
            const glm::dmat3 linear(*world);
            const auto determinant = glm::determinant(linear);
            if (std::isfinite(determinant) && determinant != 0)
            {
                const auto normal = glm::transpose(glm::inverse(linear));
                double largest = 0;
                for (int column = 0; column < 3; ++column)
                {
                    for (int row = 0; row < 3; ++row) { largest = std::max(largest, std::abs(normal[column][row])); }
                }
                if (std::isfinite(largest) && largest > 0)
                {
                    for (int column = 0; column < 3; ++column)
                    {
                        for (int row = 0; row < 3; ++row) { transform.normalFromLocal[column * 4 + row] = static_cast<float>(normal[column][row] / largest); }
                    }
                    transform.attributes = rhi::NormalTransformValid | (determinant < 0 ? rhi::Mirrored : 0u);
                }
            }
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    const auto value = matrix[column][row];
                    require(std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max(), "RenderScene matrix exceeds finite float range");
                    transform.clipFromLocal[column * 4 + row] = static_cast<float>(value);
                }
            }
            const auto& primitives = model->meshes[instance.meshIndex].primitives;
            require(!primitives.empty(), "RenderScene Mesh has no Primitives");
            for (size_t primitive = 0; primitive < primitives.size(); ++primitive)
            {
                result.push_back({entity, {instance.model, instance.meshIndex, primitive}, transform});
            }
        });
        return result;
    }
    catch (const Failure& failure) { return std::unexpected(failure.message); }
}

std::expected<GeometryBatch, std::string> prepareGeometry(const assets::ModelAssets& assets, std::span<const MeshDraw> draws)
{
    try
    {
        GeometryBatch result;
        for (const auto& draw : draws)
        {
            if (std::ranges::any_of(result.ranges, [&](const auto& range) { return range.geometry == draw.geometry; })) { continue; }
            const auto* model = assets.find(draw.geometry.model);
            require(model && draw.geometry.mesh < model->meshes.size(), "Geometry batch has an invalid asset or Mesh");
            const auto& mesh = model->meshes[draw.geometry.mesh];
            require(draw.geometry.primitive < mesh.primitives.size(), "Geometry batch has an invalid Primitive");
            const auto& primitive = mesh.primitives[draw.geometry.primitive];
            const auto materialAt = std::ranges::find_if(result.materials, [&](const auto& value) { return value.model == draw.geometry.model && value.source == primitive.material; });
            uint32_t materialIndex = static_cast<uint32_t>(materialAt - result.materials.begin());
            if (materialAt == result.materials.end())
            {
                require(result.materials.size() < UINT32_MAX, "Too many materials");
                require(!primitive.material || *primitive.material < model->materials.size(), "Primitive material is invalid");
                assets::Material material = primitive.material ? model->materials[*primitive.material] : assets::Material{};
                require(material.alphaMode != assets::AlphaMode::Blend, "BLEND materials are not supported by the opaque preview");
                for (const auto factor : material.baseColor) { require(std::isfinite(factor) && factor >= 0 && factor <= 1, "Material base color factor is invalid"); }
                require(std::isfinite(material.alphaCutoff) && material.alphaCutoff >= 0, "Material alpha cutoff is invalid");
                require(!material.baseColorTexture || *material.baseColorTexture < model->textures.size(), "Material texture is invalid");
                result.materials.push_back({draw.geometry.model, primitive.material, std::move(material)});
            }
            require(!result.materials[materialIndex].material.baseColorTexture || primitive.texcoords0.size() == primitive.positions.size(), "Base-color texture requires TEXCOORD_0");
            require(!primitive.positions.empty() && !primitive.indices.empty() && primitive.indices.size() % 3 == 0, "Primitive requires positions and triangle indices");
            require(primitive.normals.empty() || primitive.normals.size() == primitive.positions.size(), "Primitive normal count differs from positions");
            require(primitive.texcoords0.empty() || primitive.texcoords0.size() == primitive.positions.size(), "Primitive UV count differs from positions");
            constexpr size_t vertexLimit = 0x01000000;
            require(primitive.positions.size() <= vertexLimit - result.vertices.size(), "Geometry batch exceeds guaranteed vertex index range");
            require(primitive.indices.size() <= UINT32_MAX - result.indices.size(), "Geometry batch index count exceeds uint32");
            const auto base = static_cast<uint32_t>(result.vertices.size());
            const auto firstIndex = static_cast<uint32_t>(result.indices.size());
            const auto infinity = std::numeric_limits<double>::infinity();
            glm::dvec3 minimum(infinity), maximum(-infinity);
            for (const auto position : primitive.positions)
            {
                for (int axis = 0; axis < 3; ++axis) { require(std::isfinite(position[axis]), "Primitive position is nonfinite"); }
                minimum = glm::min(minimum, glm::dvec3(position));
                maximum = glm::max(maximum, glm::dvec3(position));
            }
            const auto extent = maximum - minimum;
            for (size_t index = 0; index < primitive.positions.size(); ++index)
            {
                const auto position = primitive.positions[index];
                rhi::ColoredVertex vertex{};
                for (int axis = 0; axis < 3; ++axis)
                {
                    vertex.position[axis] = position[axis];
                    const auto coordinate = extent[axis] > 0 ? (position[axis] - minimum[axis]) / extent[axis] : 0.5;
                    vertex.color[axis] = static_cast<float>(0.2 + 0.6 * std::clamp(coordinate, 0.0, 1.0));
                }
                if (!primitive.normals.empty())
                {
                    const auto normal = primitive.normals[index];
                    const auto length = glm::length(glm::dvec3(normal));
                    require(std::isfinite(length) && std::abs(length - 1) < .0001, "Primitive normal must be finite and unit length");
                    for (int axis = 0; axis < 3; ++axis) { vertex.normal[axis] = normal[axis]; }
                }
                if (!primitive.texcoords0.empty())
                {
                    for (int axis = 0; axis < 2; ++axis)
                    {
                        const auto value = primitive.texcoords0[index][axis];
                        require(std::isfinite(value), "Primitive UV is nonfinite");
                        vertex.texcoord[axis] = value;
                    }
                }
                result.vertices.push_back(vertex);
            }
            for (const auto index : primitive.indices)
            {
                require(index < primitive.positions.size(), "Primitive index is out of bounds");
                result.indices.push_back(base + index);
            }
            const uint32_t attributes = (primitive.normals.empty() ? 0u : rhi::HasNormal) | (primitive.texcoords0.empty() ? 0u : rhi::HasTexcoord);
            result.ranges.push_back({draw.geometry, firstIndex, static_cast<uint32_t>(primitive.indices.size()), attributes, materialIndex});
        }
        return result;
    }
    catch (const Failure& failure) { return std::unexpected(failure.message); }
}

std::expected<std::vector<rhi::IndexedDraw>, std::string> resolveDraws(const GeometryBatch& batch, std::span<const MeshDraw> draws)
{
    std::vector<rhi::IndexedDraw> result;
    result.reserve(draws.size());
    for (const auto& draw : draws)
    {
        const auto range = std::ranges::find_if(batch.ranges, [&](const auto& value) { return value.geometry == draw.geometry; });
        if (range == batch.ranges.end()) { return std::unexpected("Draw references geometry outside the uploaded batch"); }
        auto transform = draw.transform;
        if (range->material >= batch.materials.size()) { return std::unexpected("Draw material is outside the uploaded batch"); }
        const auto& material = batch.materials[range->material].material;
        transform.attributes = (transform.attributes & (rhi::NormalTransformValid | rhi::Mirrored)) | range->attributes |
            (material.doubleSided ? rhi::DoubleSided : 0u) | (material.alphaMode == assets::AlphaMode::Mask ? rhi::AlphaMask : 0u) |
            (material.baseColorTexture ? rhi::HasTexture : 0u);
        transform.setBaseColor(material.baseColor); transform.alphaCutoff = material.alphaCutoff;
        result.push_back({transform, range->firstIndex, range->indexCount, range->material});
    }
    return result;
}
}
