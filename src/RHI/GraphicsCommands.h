#pragma once

#include "IndexedDraw.h"
#include "GraphicsPipeline.h"
#include "ComputePipeline.h"
#include "AccelerationStructure.h"
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
    // UINT32_MAX omits an attachment; at least one attachment is required.
    virtual bool beginRendering(uint32_t color, uint32_t depth, AttachmentLoad load,
        const std::array<float, 4>& clearColor) = 0;
    virtual bool drawIndexed(uint32_t vertices, uint32_t indices, uint32_t lighting,
        std::span<const uint32_t> textures, const IndexedDraw& draw, uint32_t shadow = UINT32_MAX) = 0;
    virtual bool endRendering() = 0;
    // Fullscreen depth visualization with a color-only rendering instance.
    virtual bool displayDepth(uint32_t) { return false; }
    virtual bool bindPipeline(const GraphicsPipelineRef&) { return false; }
    virtual bool bindSampledTexture(uint32_t, uint32_t) { return false; }
    virtual bool pushConstants(std::span<const std::byte>) { return false; }
    virtual bool draw(uint32_t) { return false; }
    // Specialized UI adapter; its atlas must be declared by the calling Pass.
    virtual bool drawUi(uint32_t) { return false; }
    virtual bool dispatch(const ComputePipelineRef&, std::span<const uint32_t>, std::span<const uint32_t>,
        uint32_t, uint32_t, uint32_t, uint32_t = UINT32_MAX) { return false; }
    virtual bool uploadBuffer(uint32_t,std::span<const std::byte>) { return false; }
    virtual bool buildBottom(uint32_t,uint32_t,uint32_t) { return false; }
    virtual bool buildTop(uint32_t,std::span<const uint32_t>,std::span<const AccelerationInstance>) { return false; }
};
}
