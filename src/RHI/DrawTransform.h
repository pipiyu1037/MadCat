#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

namespace madcat::rhi
{
// One column-major float4x4 at byte offset 0. The Shader multiplies M * v.
// Recording copies these 64 bytes into command state; the caller's value does
// not need to remain alive until GPU completion. No GLM object ABI is assumed.
struct alignas(16) DrawTransform
{
    std::array<float, 16> clipFromLocal{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
    };
};
static_assert(sizeof(float) == 4 && sizeof(DrawTransform) == 64 && alignof(DrawTransform) == 16);
static_assert(offsetof(DrawTransform, clipFromLocal) == 0 && std::is_trivially_copyable_v<DrawTransform>);
}
