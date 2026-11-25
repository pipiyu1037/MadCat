#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace madcat::rhi
{
// Fixed set 1, binding 0, vertex/fragment UBO. No GLM ABI.
// Unit world direction points toward the light (opposite photon travel).
// Irradiance is linear RGB; Lambert outgoing radiance = albedo * E * NdotL / pi.
// No light is represented by zero irradiance, with a valid default direction.
struct alignas(16) FrameLighting
{
    std::array<float, 4> toLight{0, 0, 1, 0};
    std::array<float, 4> irradiance{0, 0, 0, 0};
    // lightClipFromWorld * inverse(cameraClipFromWorld), column-major.
    std::array<float, 16> shadowFromClip{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    // Depth-space constant bias, slope bias, enabled (0/1), PCF radius (0/1).
    std::array<float, 4> shadow{.001f, .003f, 0, 1};
    // PBR view reconstruction and tangent transport for both lighting modes.
    // Must match the camera/aspect used by every forward draw's clipFromLocal.
    std::array<float, 16> worldFromClip{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::array<float, 4> cameraPosition{0,0,0,1};

    [[nodiscard]] bool valid() const noexcept
    {
        double lengthSquared = 0;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(toLight[axis]) || !std::isfinite(irradiance[axis]) || irradiance[axis] < 0) { return false; }
            lengthSquared += double(toLight[axis]) * toLight[axis];
        }
        for (const auto value : shadowFromClip) { if (!std::isfinite(value)) { return false; } }
        for (const auto value : worldFromClip) { if (!std::isfinite(value)) { return false; } }
        for (const auto value : cameraPosition) { if (!std::isfinite(value)) { return false; } }
        return std::abs(lengthSquared - 1) < 1e-4 && toLight[3] == 0 && irradiance[3] == 0 &&
            std::isfinite(shadow[0]) && shadow[0]>=0 && std::isfinite(shadow[1]) && shadow[1]>=0 &&
            (shadow[2]==0 || shadow[2]==1) && (shadow[3]==0 || shadow[3]==1) && cameraPosition[3]==1;
    }
};
static_assert(sizeof(FrameLighting) == 192 && alignof(FrameLighting) == 16 &&
    offsetof(FrameLighting, irradiance) == 16 && offsetof(FrameLighting, shadowFromClip)==32 &&
    offsetof(FrameLighting, shadow)==96 && offsetof(FrameLighting, worldFromClip)==112 &&
    offsetof(FrameLighting, cameraPosition)==176 && std::is_standard_layout_v<FrameLighting> &&
    std::is_trivially_copyable_v<FrameLighting>);
}
