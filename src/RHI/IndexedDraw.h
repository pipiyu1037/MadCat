#pragma once

#include "DrawTransform.h"

#include <cstdint>
#include <array>

namespace madcat::rhi
{
// Value snapshot into the pipeline's retained immutable geometry buffers.
struct IndexedDraw
{
    DrawTransform transform;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t material = 0;
    // CPU instance snapshot for AS construction; not part of the draw Shader ABI.
    std::array<float,12> worldFromLocal{1,0,0,0,0,1,0,0,0,0,1,0};
    bool affineWorld = true;
};
}
