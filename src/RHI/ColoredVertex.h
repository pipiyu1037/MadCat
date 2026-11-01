#pragma once

#include <cstddef>
#include <type_traits>

namespace madcat::rhi
{
// Explicit upload ABI, independent of GLM alignment and Vulkan types.
struct ColoredVertex
{
    float position[3];
    float color[3];
};
static_assert(std::is_standard_layout_v<ColoredVertex> && std::is_trivially_copyable_v<ColoredVertex>);
static_assert(sizeof(ColoredVertex) == 24 && offsetof(ColoredVertex, position) == 0 && offsetof(ColoredVertex, color) == 12);
}
