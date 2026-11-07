#include "VulkanDepthViewPipeline.h"
#include "VulkanDevice.h"
#include "VulkanShaderModule.h"
#include "VulkanSampledTexture.h"
#include "VulkanFunctions.h"
#include <type_traits>

namespace madcat::rhi
{
struct VulkanDepthViewPipeline::State
{
    VkDevice device = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    PFN_vkDestroyDescriptorSetLayout destroySet = nullptr;
    PFN_vkDestroyPipelineLayout destroyLayout = nullptr;
    PFN_vkDestroyPipeline destroyPipeline = nullptr;
    PFN_vkCmdBindPipeline bindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets bindSets = nullptr;
    PFN_vkCmdSetViewport viewport = nullptr;
    PFN_vkCmdSetScissor scissor = nullptr;
    PFN_vkCmdDraw draw = nullptr;
    ~State()
    {
        if (pipeline) { destroyPipeline(device, pipeline, nullptr); }
        if (layout) { destroyLayout(device, layout, nullptr); }
        if (setLayout) { destroySet(device, setLayout, nullptr); }
    }
};
VulkanDepthViewPipeline::VulkanDepthViewPipeline() = default;
VulkanDepthViewPipeline::~VulkanDepthViewPipeline()
{
    if (state_) { state_.reset(); std::fprintf(stderr,"Vulkan: Depth view pipeline destroyed.\n"); }
}
VkDevice VulkanDepthViewPipeline::deviceHandle() const { return state_ ? state_->device : VK_NULL_HANDLE; }
VkFormat VulkanDepthViewPipeline::colorFormat() const { return state_ ? state_->format : VK_FORMAT_UNDEFINED; }
bool VulkanDepthViewPipeline::initialize(const VulkanDevice& device, VkFormat format,
    const VulkanShaderModule& vertex, const VulkanShaderModule& fragment)
{
    if (state_ || !device.handle() || !vertex.handle() || !fragment.handle() ||
        vertex.deviceHandle()!=device.handle() || fragment.deviceHandle()!=device.handle() || format==VK_FORMAT_UNDEFINED) { return false; }
    auto state = std::make_unique<State>(); state->device=device.handle(); state->format=format;
    const auto getProc = detail::loadFunction<PFN_vkGetDeviceProcAddr>(device.physicalDeviceInfo().instance,"vkGetDeviceProcAddr");
    if (!getProc) { return false; }
    const auto load = [&](auto& fn, const char* name) {
        fn=reinterpret_cast<std::remove_reference_t<decltype(fn)>>(getProc(device.handle(),name));
        if (!fn) { std::fprintf(stderr,"Vulkan error: depth viewer requires %s.\n",name); }
        return fn!=nullptr;
    };
    PFN_vkCreateDescriptorSetLayout createSet{};
    PFN_vkCreatePipelineLayout createLayout{};
    PFN_vkCreateGraphicsPipelines createPipelines{};
    if (!load(createSet,"vkCreateDescriptorSetLayout") || !load(state->destroySet,"vkDestroyDescriptorSetLayout") ||
        !load(createLayout,"vkCreatePipelineLayout") || !load(state->destroyLayout,"vkDestroyPipelineLayout") ||
        !load(createPipelines,"vkCreateGraphicsPipelines") || !load(state->destroyPipeline,"vkDestroyPipeline") ||
        !load(state->bindPipeline,"vkCmdBindPipeline") || !load(state->bindSets,"vkCmdBindDescriptorSets") ||
        !load(state->viewport,"vkCmdSetViewport") || !load(state->scissor,"vkCmdSetScissor") || !load(state->draw,"vkCmdDraw")) { return false; }
    const VkDescriptorSetLayoutBinding bindings[] = {
        {0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {1,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount=2; set.pBindings=bindings;
    VkDescriptorSetLayout createdSet = VK_NULL_HANDLE;
    if (!detail::checkResult(createSet(device.handle(),&set,nullptr,&createdSet),"create depth viewer set layout")) { return false; }
    state->setLayout=createdSet;
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount=1; layout.pSetLayouts=&state->setLayout;
    VkPipelineLayout createdLayout = VK_NULL_HANDLE;
    if (!detail::checkResult(createLayout(device.handle(),&layout,nullptr,&createdLayout),"create depth viewer pipeline layout")) { return false; }
    state->layout=createdLayout;
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
    if (!detail::checkResult(createPipelines(device.handle(),VK_NULL_HANDLE,1,&pipeline,nullptr,&state->pipeline),"create depth viewer pipeline")) { return false; }
    state_=std::move(state); std::fprintf(stderr,"Vulkan: Depth view pipeline created.\n"); return true;
}
bool VulkanDepthViewPipeline::record(VkCommandBuffer command, VkExtent2D extent, const TextureBindingRef& binding) const
{
    if (!state_ || !command || !extent.width || !extent.height || detail::TextureBindingAccess::device(binding)!=state_->device) { return false; }
    const auto& state=*state_;
    const VkViewport viewport{0,0,float(extent.width),float(extent.height),0,1};
    const VkRect2D scissor{{0,0},extent};
    const auto set=detail::TextureBindingAccess::set(binding);
    state.bindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,state.pipeline);
    state.viewport(command,0,1,&viewport); state.scissor(command,0,1,&scissor);
    state.bindSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,state.layout,0,1,&set,0,nullptr);
    state.draw(command,3,1,0,0); return true;
}
}
