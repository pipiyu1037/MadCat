#pragma once

#include "ECS/Entity.h"

#include <glm/glm.hpp>

#include <optional>

namespace madcat::scene
{
class Scene;

// Attach to a Scene Entity. Pose comes from its world transform; local -Z is
// forward and +Y is up. Aspect ratio comes from the output, not the component.
struct PerspectiveCamera
{
    float verticalFov = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
};

struct CameraMatrices
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
};

// Read-only, uncached value snapshot. Requires a current Scene world transform
// and PerspectiveCamera on this Entity. View is the full inverse world matrix,
// including inherited scale/shear; use unit scales for an undistorted camera.
// Projection uses radians, right-handed -Z, near -> 0 and far -> 1 (no reversed
// depth). Y is flipped here for a positive-height Vulkan viewport; do not also
// flip in the viewport/Shader. viewProjection = projection * view (column vectors).
// Returns nullopt for missing/stale data, nonfinite/invalid parameters (0 < FOV
// < pi, aspect > 0, 0 < near < far), singular world transforms, or results that
// overflow or become singular when stored as float. No scene mutation/allocation.
// The value does not track later edits; rebuild it after updating the scene.
[[nodiscard]] std::optional<CameraMatrices> buildCameraMatrices(
    const Scene& scene, ecs::Entity entity, float aspectRatio) noexcept;
}
