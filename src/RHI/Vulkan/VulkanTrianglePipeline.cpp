#include "VulkanTrianglePipeline.h"

#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanFunctions.h"
#include "VulkanShaderModule.h"

#include <type_traits>
#include <utility>

namespace madcat::rhi
{
struct VulkanTrianglePipeline::State
{
    BufferRef vertices;
    VkDevice device = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    PFN_vkCreatePipelineLayout createLayout = nullptr;
    PFN_vkDestroyPipelineLayout destroyLayout = nullptr;
    PFN_vkCreateGraphicsPipelines createPipelines = nullptr;
    PFN_vkDestroyPipeline destroyPipeline = nullptr;
    PFN_vkCmdBindPipeline bindPipeline = nullptr;
    PFN_vkCmdSetViewport setViewport = nullptr;
    PFN_vkCmdSetScissor setScissor = nullptr;
    PFN_vkCmdDraw draw = nullptr;
    PFN_vkCmdBindVertexBuffers bindVertices = nullptr;

    bool loadFunctions(VkInstance instance)
    {
        const auto getProc = detail::loadFunction<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
        if (!getProc) { return false; }
        const auto load = [&](auto& function, const char* name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(getProc(device, name));
            if (!function) { std::fprintf(stderr, "Vulkan error: required device function %s is unavailable.\n", name); }
            return function != nullptr;
        };
        // Resolve all operations, including destruction, before acquiring objects.
        return load(createLayout, "vkCreatePipelineLayout") && load(destroyLayout, "vkDestroyPipelineLayout") &&
            load(createPipelines, "vkCreateGraphicsPipelines") && load(destroyPipeline, "vkDestroyPipeline") &&
            load(bindPipeline, "vkCmdBindPipeline") && load(setViewport, "vkCmdSetViewport") &&
            load(setScissor, "vkCmdSetScissor") && load(draw, "vkCmdDraw") && load(bindVertices, "vkCmdBindVertexBuffers");
    }

    ~State()
    {
        if (pipeline) { destroyPipeline(device, pipeline, nullptr); }
        if (layout) { destroyLayout(device, layout, nullptr); }
    }
};

VulkanTrianglePipeline::VulkanTrianglePipeline() = default;

VulkanTrianglePipeline::~VulkanTrianglePipeline()
{
    if (state_)
    {
        state_.reset();
        std::fprintf(stderr, "Vulkan: Triangle pipeline destroyed.\n");
    }
}

bool VulkanTrianglePipeline::initialize(const VulkanDevice& device, VkFormat colorFormat,
    const VulkanShaderModule& vertex, const VulkanShaderModule& fragment, BufferRef vertices)
{
    if (state_)
    {
        std::fprintf(stderr, "Vulkan error: this triangle pipeline is already initialized.\n");
        return false;
    }
    if (!device.handle() || !vertex.handle() || !fragment.handle() ||
        vertex.deviceHandle() != device.handle() || fragment.deviceHandle() != device.handle() || colorFormat == VK_FORMAT_UNDEFINED ||
        detail::BufferAccess::device(vertices) != device.handle() || vertices.sizeBytes() != 3 * sizeof(TriangleVertex))
    {
        std::fprintf(stderr, "Vulkan error: triangle pipeline requires same-Device Shader Modules, three vertices and a color format.\n");
        return false;
    }
    auto created = std::make_unique<State>();
    created->device = device.handle();
    created->vertices = std::move(vertices);
    created->format = colorFormat;
    if (!created->loadFunctions(device.physicalDeviceInfo().instance)) { return false; }
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkPipelineLayout createdLayout = VK_NULL_HANDLE;
    if (!detail::checkResult(created->createLayout(created->device, &layout, nullptr, &createdLayout), "vkCreatePipelineLayout")) { return false; }
    created->layout = createdLayout;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex.handle();
    stages[0].pName = "vertexMain";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment.handle();
    stages[1].pName = "fragmentMain";
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    static_assert(std::is_standard_layout_v<TriangleVertex> && sizeof(TriangleVertex) == 5 * sizeof(float));
    const VkVertexInputBindingDescription binding{0, sizeof(TriangleVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TriangleVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TriangleVertex, color)}
    };
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{};
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo samples{};
    samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState color{};
    color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &color;
    constexpr VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &created->format;
    VkGraphicsPipelineCreateInfo pipeline{};
    pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline.pNext = &rendering;
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &vertexInput;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &samples;
    pipeline.pDepthStencilState = &depth;
    pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = created->layout;
    pipeline.basePipelineIndex = -1;
    // Pipeline creation defines every output as either a valid handle or null,
    // even on failure; State releases any successful output and the layout.
    if (!detail::checkResult(created->createPipelines(created->device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &created->pipeline), "vkCreateGraphicsPipelines"))
    {
        std::fprintf(stderr, "Vulkan error: triangle pipeline failed; vertexMain / fragmentMain, color format %d.\n", static_cast<int>(colorFormat));
        return false;
    }
    state_ = std::move(created);
    std::fprintf(stderr, "Vulkan: Triangle pipeline created; vertexMain / fragmentMain, color format %d, dynamic viewport/scissor.\n", static_cast<int>(colorFormat));
    return true;
}

VkPipeline VulkanTrianglePipeline::handle() const { return state_ ? state_->pipeline : VK_NULL_HANDLE; }
VkDevice VulkanTrianglePipeline::deviceHandle() const { return state_ ? state_->device : VK_NULL_HANDLE; }
VkFormat VulkanTrianglePipeline::colorFormat() const { return state_ ? state_->format : VK_FORMAT_UNDEFINED; }
BufferRef VulkanTrianglePipeline::vertexBuffer() const { return state_ ? state_->vertices : BufferRef{}; }

bool VulkanTrianglePipeline::recordDraw(VkCommandBuffer commandBuffer, VkExtent2D extent) const
{
    if (!state_ || !commandBuffer || extent.width == 0 || extent.height == 0)
    {
        std::fprintf(stderr, "Vulkan error: triangle Draw requires an initialized pipeline, recording command buffer and nonzero extent.\n");
        return false;
    }
    const auto& state = *state_;
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    const VkRect2D scissor{{0, 0}, extent};
    state.bindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
    state.setViewport(commandBuffer, 0, 1, &viewport);
    state.setScissor(commandBuffer, 0, 1, &scissor);
    const auto buffer = detail::BufferAccess::handle(state.vertices);
    constexpr VkDeviceSize offset = 0;
    state.bindVertices(commandBuffer, 0, 1, &buffer, &offset);
    state.draw(commandBuffer, 3, 1, 0, 0);
    return true;
}
}
