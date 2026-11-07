#pragma once

#include "IndexedDraw.h"
#include <array>
#include <cstdint>
#include <span>

namespace madcat::rhi
{
// Resource slots refer to the current frame's imported resource table, never
// native handles. The backend validates and retains the actual GPU resources.
enum class AttachmentLoad { Clear, Load };
class GraphicsCommands
{
public:
    virtual ~GraphicsCommands() = default;
    virtual bool beginRendering(uint32_t color, uint32_t depth, AttachmentLoad load,
        const std::array<float, 4>& clearColor) = 0;
    virtual bool drawIndexed(uint32_t vertices, uint32_t indices, uint32_t lighting,
        std::span<const uint32_t> textures, const IndexedDraw& draw) = 0;
    virtual bool endRendering() = 0;
    // Fullscreen depth visualization with a color-only rendering instance.
    virtual bool displayDepth(uint32_t) { return false; }
};
}
