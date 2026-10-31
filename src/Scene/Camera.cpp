#include "Camera.h"

#include "Scene.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <limits>

namespace madcat::scene
{
namespace
{
bool storeMatrix(const glm::dmat4& source, glm::mat4& destination) noexcept
{
    // Check before narrowing: even finite double intermediates may exceed float.
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            const auto value = source[column][row];
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) { return false; }
            destination[column][row] = static_cast<float>(value);
        }
    }
    // Narrowing can erase a necessary coefficient even without overflow.
    return glm::determinant(glm::dmat4(destination)) != 0.0;
}
}

std::optional<CameraMatrices> buildCameraMatrices(
    const Scene& scene, ecs::Entity entity, float aspectRatio) noexcept
{
    const auto* camera = scene.tryGet<PerspectiveCamera>(entity);
    const auto* world = scene.worldTransform(entity);
    if (!camera || !world || !std::isfinite(camera->verticalFov) ||
        !std::isfinite(aspectRatio) || !std::isfinite(camera->nearPlane) ||
        !std::isfinite(camera->farPlane) || camera->verticalFov <= 0.0f ||
        static_cast<double>(camera->verticalFov) >= glm::pi<double>() || aspectRatio <= 0.0f ||
        camera->nearPlane <= 0.0f || camera->farPlane <= camera->nearPlane) { return std::nullopt; }

    const glm::dmat4 worldMatrix(*world);
    if (glm::determinant(worldMatrix) == 0.0) { return std::nullopt; }

    CameraMatrices result;
    if (!storeMatrix(glm::inverse(worldMatrix), result.view)) { return std::nullopt; }

    // Explicit RH zero-to-one perspective. Double intermediates keep finite
    // float near/far products and very narrow FOVs from overflowing prematurely.
    const double near = camera->nearPlane;
    const double far = camera->farPlane;
    const double focalLength = 1.0 / std::tan(static_cast<double>(camera->verticalFov) / 2.0);
    glm::dmat4 projection(0.0);
    projection[0][0] = focalLength / aspectRatio;
    projection[1][1] = -focalLength;
    projection[2][2] = far / (near - far);
    projection[2][3] = -1.0;
    projection[3][2] = near * far / (near - far);
    if (!storeMatrix(projection, result.projection) ||
        !storeMatrix(glm::dmat4(result.projection) * glm::dmat4(result.view), result.viewProjection))
    {
        return std::nullopt;
    }
    return result;
}
}
