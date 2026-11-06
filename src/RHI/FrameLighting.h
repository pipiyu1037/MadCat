#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace madcat::rhi
{
// Fixed set 1, binding 0, fragment Uniform Buffer. Two float4 values, no GLM ABI.
// Unit world direction points toward the light (opposite photon travel).
// Irradiance is linear RGB; Lambert outgoing radiance = albedo * E * NdotL / pi.
// No light is represented by zero irradiance, with a valid default direction.
struct alignas(16) FrameLighting
{
    std::array<float, 4> toLight{0, 0, 1, 0};
    std::array<float, 4> irradiance{0, 0, 0, 0};

    [[nodiscard]] bool valid() const noexcept
    {
        double lengthSquared = 0;
        for (size_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(toLight[axis]) || !std::isfinite(irradiance[axis]) || irradiance[axis] < 0) { return false; }
            lengthSquared += double(toLight[axis]) * toLight[axis];
        }
        return std::abs(lengthSquared - 1) < 1e-4 && toLight[3] == 0 && irradiance[3] == 0;
    }
};
static_assert(sizeof(FrameLighting) == 32 && alignof(FrameLighting) == 16 &&
    offsetof(FrameLighting, irradiance) == 16 && std::is_standard_layout_v<FrameLighting> &&
    std::is_trivially_copyable_v<FrameLighting>);
}
