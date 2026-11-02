#pragma once

#include "DrawTransform.h"

#include <cstdint>

namespace madcat::rhi
{
// Value snapshot into the pipeline's retained immutable geometry buffers.
struct IndexedDraw
{
    DrawTransform transform;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};
}
