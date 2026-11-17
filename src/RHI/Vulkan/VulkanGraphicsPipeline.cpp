#include "VulkanGraphicsPipeline.h"
#include "VulkanDevice.h"
#include "VulkanShaderModule.h"
#include "VulkanSampledTexture.h"
#include "VulkanFunctions.h"
#include <type_traits>

namespace madcat::rhi::detail
{
struct GraphicsPipelineState
{
    VkDevice device = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t constantBytes = 0;
    bool sampled = false;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    PFN_vkDestroyDescriptorSetLayout destroySet = nullptr;
    PFN_vkDestroyPipelineLayout destroyLayout = nullptr;
    PFN_vkDestroyPipeline destroyPipeline = nullptr;
    PFN_vkCmdBindPipeline bindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets bindSets = nullptr;
    PFN_vkCmdPushConstants push = nullptr;
    PFN_vkCmdSetViewport viewport = nullptr;
    PFN_vkCmdSetScissor scissor = nullptr;
    PFN_vkCmdDraw draw = nullptr;
    ~GraphicsPipelineState()
    {
        if (pipeline) { destroyPipeline(device, pipeline, nullptr); }
        if (layout) { destroyLayout(device, layout, nullptr); }
        if (setLayout) { destroySet(device, setLayout, nullptr); }
        if (pipeline) { std::fprintf(stderr,"Vulkan: Graphics pipeline destroyed.\n"); }
    }
};
VkDevice GraphicsPipelineAccess::device(const GraphicsPipelineRef& ref) { return ref.state_ ? ref.state_->device : VK_NULL_HANDLE; }
VkFormat GraphicsPipelineAccess::format(const GraphicsPipelineRef& ref) { return ref.state_ ? ref.state_->format : VK_FORMAT_UNDEFINED; }
uint32_t GraphicsPipelineAccess::constantBytes(const GraphicsPipelineRef& ref) { return ref.state_ ? ref.state_->constantBytes : 0; }
bool GraphicsPipelineAccess::samplesTexture(const GraphicsPipelineRef& ref) { return ref.state_ && ref.state_->sampled; }
GraphicsPipelineRef GraphicsPipelineAccess::create(const VulkanDevice& device, VkFormat presentationFormat, const GraphicsPipelineDesc& desc)
{
    if (!device.handle() || desc.pushConstantBytes > 128 || desc.pushConstantBytes % 4 ||
        (desc.colorFormat != ColorFormat::Presentation && desc.colorFormat != ColorFormat::Rgba16Float))
    { std::fprintf(stderr,"Vulkan error: unsupported programmable graphics pipeline description.\n"); return {}; }
    const auto format = desc.colorFormat == ColorFormat::Presentation ? presentationFormat : VK_FORMAT_R16G16B16A16_SFLOAT;
    if (format == VK_FORMAT_UNDEFINED) { return {}; }
    VulkanShaderModule vertex, fragment;
    if (!vertex.initialize(device,desc.vertexShader) || !fragment.initialize(device,desc.fragmentShader)) { return {}; }
    auto state = std::make_shared<GraphicsPipelineState>();
    state->device=device.handle(); state->format=format; state->sampled=desc.sampledTexture; state->constantBytes=desc.pushConstantBytes;
    const auto getProc = loadFunction<PFN_vkGetDeviceProcAddr>(device.physicalDeviceInfo().instance,"vkGetDeviceProcAddr");
    if (!getProc) { return {}; }
    const auto load = [&](auto& fn, const char* name) {
        fn=reinterpret_cast<std::remove_reference_t<decltype(fn)>>(getProc(device.handle(),name));
        if (!fn) { std::fprintf(stderr,"Vulkan error: graphics pipeline requires %s.\n",name); }
        return fn!=nullptr;
    };
    PFN_vkCreateDescriptorSetLayout createSet{};
    PFN_vkCreatePipelineLayout createLayout{};
    PFN_vkCreateGraphicsPipelines createPipelines{};
    if (!load(createSet,"vkCreateDescriptorSetLayout") || !load(state->destroySet,"vkDestroyDescriptorSetLayout") ||
        !load(createLayout,"vkCreatePipelineLayout") || !load(state->destroyLayout,"vkDestroyPipelineLayout") ||
        !load(createPipelines,"vkCreateGraphicsPipelines") || !load(state->destroyPipeline,"vkDestroyPipeline") ||
        !load(state->bindPipeline,"vkCmdBindPipeline") || !load(state->bindSets,"vkCmdBindDescriptorSets") ||
        !load(state->push,"vkCmdPushConstants") || !load(state->viewport,"vkCmdSetViewport") ||
        !load(state->scissor,"vkCmdSetScissor") || !load(state->draw,"vkCmdDraw")) { return {}; }
    if (desc.sampledTexture)
    {
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.bindingCount=2; set.pBindings=bindings;
        if (!checkResult(createSet(device.handle(),&set,nullptr,&state->setLayout),"create graphics set layout")) { return {}; }
    }
    constexpr auto constantStages = VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkPushConstantRange constants{constantStages,0,desc.pushConstantBytes};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount=desc.sampledTexture ? 1 : 0; layout.pSetLayouts=desc.sampledTexture ? &state->setLayout : nullptr;
    layout.pushConstantRangeCount=desc.pushConstantBytes ? 1 : 0; layout.pPushConstantRanges=desc.pushConstantBytes ? &constants : nullptr;
    if (!checkResult(createLayout(device.handle(),&layout,nullptr,&state->layout),"create graphics pipeline layout")) { return {}; }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType=stages[1].sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vertex.handle(); stages[0].pName="vertexMain";
    stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=fragment.handle(); stages[1].pName="fragmentMain";
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount=viewport.scissorCount=1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.lineWidth=1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState color{}; color.colorWriteMask=0xf;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount=1; blend.pAttachments=&color;
    const VkDynamicState dynamics[]{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount=2; dynamic.pDynamicStates=dynamics;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount=1; rendering.pColorAttachmentFormats=&format;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.pNext=&rendering; pipeline.stageCount=2; pipeline.pStages=stages;
    pipeline.pVertexInputState=&input; pipeline.pInputAssemblyState=&assembly; pipeline.pViewportState=&viewport;
    pipeline.pRasterizationState=&raster; pipeline.pMultisampleState=&samples; pipeline.pColorBlendState=&blend;
    pipeline.pDynamicState=&dynamic; pipeline.layout=state->layout; pipeline.basePipelineIndex=-1;
    if (!checkResult(createPipelines(device.handle(),VK_NULL_HANDLE,1,&pipeline,nullptr,&state->pipeline),"create graphics pipeline")) { return {}; }
    GraphicsPipelineRef ref; ref.state_=std::move(state);
    std::fprintf(stderr,"Vulkan: Graphics pipeline created; color format %d, sampled %d, constants %u.\n",int(format),int(desc.sampledTexture),desc.pushConstantBytes);
    return ref;
}
bool GraphicsPipelineAccess::bind(const GraphicsPipelineRef& ref, VkCommandBuffer command, VkExtent2D extent)
{
    if (!ref || !command || !extent.width || !extent.height) { return false; }
    const auto& state=*ref.state_;
    const VkViewport viewport{0,0,float(extent.width),float(extent.height),0,1};
    const VkRect2D scissor{{0,0},extent};
    state.bindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,state.pipeline);
    state.viewport(command,0,1,&viewport); state.scissor(command,0,1,&scissor); return true;
}
bool GraphicsPipelineAccess::bindTexture(const GraphicsPipelineRef& ref, VkCommandBuffer command, const TextureBindingRef& binding)
{
    if (!ref || !command || !samplesTexture(ref) || TextureBindingAccess::device(binding)!=device(ref)) { return false; }
    const auto set=TextureBindingAccess::set(binding);
    ref.state_->bindSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,ref.state_->layout,0,1,&set,0,nullptr); return true;
}
bool GraphicsPipelineAccess::push(const GraphicsPipelineRef& ref, VkCommandBuffer command, std::span<const std::byte> bytes)
{
    if (!ref || !command || bytes.empty() || bytes.size()!=constantBytes(ref)) { return false; }
    ref.state_->push(command,ref.state_->layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,
        static_cast<uint32_t>(bytes.size()),bytes.data()); return true;
}
bool GraphicsPipelineAccess::draw(const GraphicsPipelineRef& ref, VkCommandBuffer command, uint32_t vertexCount)
{
    if (!ref || !command || !vertexCount || vertexCount%3) { return false; }
    ref.state_->draw(command,vertexCount,1,0,0); return true;
}
}
