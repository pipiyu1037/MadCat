#pragma once

#include "RHI/Texture.h"
#include "RHI/IndexedDraw.h"
#include "RenderGraph/GraphicsGraph.h"
#include <vulkan/vulkan.h>
#include <memory>

namespace madcat::rhi
{
class VulkanDevice;
class VulkanTrianglePipeline;
class VulkanDepthViewPipeline;
namespace detail
{
class VulkanFrameLighting;
// Backend adapter for a single acquired image. Owns a compiled CPU plan and
// import mapping; borrows the pipeline and frame UBO until commands retire.
class VulkanFrameGraph
{
public:
    VulkanFrameGraph();
    ~VulkanFrameGraph();
    VulkanFrameGraph(const VulkanFrameGraph&) = delete;
    VulkanFrameGraph& operator=(const VulkanFrameGraph&) = delete;
    [[nodiscard]] bool initialize(const VulkanDevice& device, VkImage image, VkImageView view, VkExtent2D extent,
        TextureRef depth, const VulkanTrianglePipeline* pipeline, const VulkanFrameLighting& lighting,
        const graph::GraphicsGraphFactory& factory, const VulkanDepthViewPipeline* depthView = nullptr);
    [[nodiscard]] bool record(VkCommandBuffer command, const std::array<float,4>& color,
        std::span<const IndexedDraw> draws, bool presented, bool depthInitialized);
    // Called only after successful queue submission; recording alone publishes no state.
    [[nodiscard]] bool submitted(); // returns whether external depth is initialized
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
}
