#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace madcat::rhi
{
enum class GeometryView : uint32_t { Color, WorldNormal, Texcoord, Material };
enum GeometryAttribute : uint32_t { HasNormal = 1, HasTexcoord = 2, NormalTransformValid = 4,
    Mirrored = 8, DoubleSided = 16, AlphaMask = 32, HasTexture = 64 };

// Column-major float4x4, three normal columns (xyz) with base RGB in their w,
// view/flags/base alpha/cutoff. Shares 128 bytes without a larger device limit.
// Normal matrix may be scaled by one positive constant; normalize after interpolation.
// Recording copies these 128 bytes into command state; the caller's value does
// not need to remain alive until GPU completion. No GLM object ABI is assumed.
struct alignas(16) DrawTransform
{
    std::array<float, 16> clipFromLocal{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
    };
    std::array<float, 12> normalFromLocal{1,0,0,1, 0,1,0,1, 0,0,1,1};
    GeometryView view = GeometryView::Color;
    uint32_t attributes = NormalTransformValid;
    float baseColorAlpha = 1, alphaCutoff = .5f;

    void setBaseColor(const std::array<float, 4>& factor)
    {
        normalFromLocal[3] = factor[0]; normalFromLocal[7] = factor[1]; normalFromLocal[11] = factor[2]; baseColorAlpha = factor[3];
    }

    [[nodiscard]] bool valid() const
    {
        return static_cast<uint32_t>(view) <= static_cast<uint32_t>(GeometryView::Material) && (attributes & ~127u) == 0 &&
            std::isfinite(baseColorAlpha) && std::isfinite(alphaCutoff) &&
            std::ranges::all_of(clipFromLocal, [](float value) { return std::isfinite(value); }) &&
            std::ranges::all_of(normalFromLocal, [](float value) { return std::isfinite(value); });
    }
};
static_assert(sizeof(float) == 4 && sizeof(DrawTransform) == 128 && alignof(DrawTransform) == 16);
static_assert(offsetof(DrawTransform, clipFromLocal) == 0 && offsetof(DrawTransform, normalFromLocal) == 64 &&
    offsetof(DrawTransform, view) == 112 && offsetof(DrawTransform, attributes) == 116 &&
    offsetof(DrawTransform, baseColorAlpha) == 120 && offsetof(DrawTransform, alphaCutoff) == 124 &&
    std::is_standard_layout_v<DrawTransform> && std::is_trivially_copyable_v<DrawTransform>);
}
