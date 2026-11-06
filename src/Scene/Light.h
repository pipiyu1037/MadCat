#pragma once

#include <glm/vec3.hpp>

namespace madcat::scene
{
// Attach to a Scene Entity. Local -Z is the direction light travels;
// normalize the transformed axis after inheriting the full world transform.
// Translation has no effect. Use unit scales for an undistorted light rig.
// Linear RGB and irradiance scale must be finite and nonnegative.
struct DirectionalLight
{
    glm::vec3 color{1.0f};
    float irradiance = 3.14159265f;
};
}
