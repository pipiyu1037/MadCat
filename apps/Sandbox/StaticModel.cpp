#include "StaticModel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace madcat::sandbox
{
namespace
{
struct Failure { std::string message; };
void require(bool condition, std::string_view message)
{
    if (!condition) { throw Failure{std::string(message)}; }
}
}

std::expected<StaticModel, std::string> prepareStaticModel(const assets::GltfGeometry& geometry)
{
    try
    {
        StaticModel result;
        std::vector<glm::dvec3> positions;
        std::vector<bool> visited(geometry.nodes.size());
        auto ordered = geometry.sceneRoots;
        const auto infinity = std::numeric_limits<double>::infinity();
        glm::dvec3 minimum(infinity), maximum(-infinity);
        for (size_t cursor = 0; cursor < ordered.size(); ++cursor)
        {
            const auto index = ordered[cursor];
            require(index < geometry.nodes.size() && !visited[index], "invalid or repeated active node");
            visited[index] = true;
            const auto& node = geometry.nodes[index];
            ordered.insert(ordered.end(), node.children.begin(), node.children.end());
            if (!node.mesh) { continue; }
            require(*node.mesh < geometry.meshes.size(), "invalid active mesh index");
            const auto& mesh = geometry.meshes[*node.mesh];
            require(!mesh.primitives.empty(), "active mesh has no primitives");
            const glm::dmat4 world(node.worldTransform);
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row) { require(std::isfinite(world[column][row]), "nonfinite active node matrix"); }
            }
            require(world[0][3] == 0 && world[1][3] == 0 && world[2][3] == 0 && world[3][3] == 1, "active node matrix must be affine");
            ++result.nodeInstances;
            for (const auto& primitive : mesh.primitives)
            {
                require(!primitive.positions.empty() && !primitive.indices.empty() && primitive.indices.size() % 3 == 0,
                    "active primitive requires positions and triangle indices");
                // Device leaves fullDrawIndexUint32 disabled. Keep merged vertex
                // indices in the guaranteed range before allocating upload bytes.
                constexpr size_t vertexLimit = 0x01000000;
                require(primitive.positions.size() <= vertexLimit - positions.size(), "merged geometry exceeds preview vertex limit");
                require(primitive.indices.size() <= UINT32_MAX - result.indices.size(), "merged index count exceeds uint32");
                const auto base = static_cast<uint32_t>(positions.size());
                for (const auto position : primitive.positions)
                {
                    const auto transformed = glm::dvec3(world * glm::dvec4(position, 1));
                    for (int axis = 0; axis < 3; ++axis) { require(std::isfinite(transformed[axis]), "nonfinite transformed position"); }
                    minimum = glm::min(minimum, transformed);
                    maximum = glm::max(maximum, transformed);
                    positions.push_back(transformed);
                }
                for (const auto vertex : primitive.indices)
                {
                    require(vertex < primitive.positions.size(), "active primitive index is out of bounds");
                    result.indices.push_back(base + vertex);
                }
                ++result.primitives;
            }
        }
        require(!positions.empty(), "selected scene has no triangle geometry");
        const auto extent = maximum - minimum;
        const auto largest = std::max({extent.x, extent.y, extent.z});
        require(std::isfinite(largest) && largest > 0, "selected geometry has no finite spatial extent");
        result.assetCenter = minimum + extent * 0.5;
        result.previewScale = 1.2 / largest;
        result.vertices.reserve(positions.size());
        for (const auto position : positions)
        {
            rhi::ColoredVertex vertex{};
            for (int axis = 0; axis < 3; ++axis)
            {
                vertex.position[axis] = static_cast<float>((position[axis] - result.assetCenter[axis]) * result.previewScale);
                const auto coordinate = extent[axis] > 0 ? (position[axis] - minimum[axis]) / extent[axis] : 0.5;
                vertex.color[axis] = static_cast<float>(0.2 + 0.6 * std::clamp(coordinate, 0.0, 1.0));
            }
            result.vertices.push_back(vertex);
        }
        return result;
    }
    catch (const Failure& failure) { return std::unexpected(failure.message); }
}
}
