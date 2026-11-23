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
    float normal[3]{};
    float texcoord[2]{};
    // Startup materials are immutable and primitive-local. Repeated factors
    // share the existing vertex resource/read declaration; no material UBO yet.
    float metallicRoughness[2]{1,1};
};
static_assert(std::is_standard_layout_v<ColoredVertex> && std::is_trivially_copyable_v<ColoredVertex>);
static_assert(sizeof(ColoredVertex) == 52 && offsetof(ColoredVertex, position) == 0 && offsetof(ColoredVertex, color) == 12 &&
    offsetof(ColoredVertex, normal) == 24 && offsetof(ColoredVertex, texcoord) == 36 && offsetof(ColoredVertex, metallicRoughness) == 44);
}
