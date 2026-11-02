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
            require(!primitive.positions.empty() && !primitive.indices.empty() && primitive.indices.size() % 3 == 0, "Primitive requires positions and triangle indices");
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
            for (const auto position : primitive.positions)
            {
                rhi::ColoredVertex vertex{};
                for (int axis = 0; axis < 3; ++axis)
                {
                    vertex.position[axis] = position[axis];
                    const auto coordinate = extent[axis] > 0 ? (position[axis] - minimum[axis]) / extent[axis] : 0.5;
                    vertex.color[axis] = static_cast<float>(0.2 + 0.6 * std::clamp(coordinate, 0.0, 1.0));
                }
                result.vertices.push_back(vertex);
            }
            for (const auto index : primitive.indices)
            {
                require(index < primitive.positions.size(), "Primitive index is out of bounds");
                result.indices.push_back(base + index);
            }
            result.ranges.push_back({draw.geometry, firstIndex, static_cast<uint32_t>(primitive.indices.size())});
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
        result.push_back({draw.transform, range->firstIndex, range->indexCount});
    }
    return result;
}
}
