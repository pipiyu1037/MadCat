#include "VulkanComputePipeline.h"
#include "VulkanBuffer.h"
#include "VulkanAccelerationStructure.h"
#include "VulkanDevice.h"
#include "VulkanFunctions.h"
#include "VulkanShaderModule.h"
#include <array>
#include <algorithm>
#include <type_traits>
#include <vector>

namespace madcat::rhi::detail
{
struct ComputePipelineState
{
    VkDevice device=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;
    uint32_t reads=0, writes=0;
    bool acceleration=false;
    std::array<uint32_t,3> maxGroups{};
    PFN_vkDestroyPipeline destroyPipeline=nullptr;
    PFN_vkDestroyPipelineLayout destroyLayout=nullptr;
    PFN_vkDestroyDescriptorSetLayout destroySet=nullptr;
    PFN_vkCmdBindPipeline bindPipeline=nullptr;
    PFN_vkCmdBindDescriptorSets bindSets=nullptr;
    PFN_vkCmdDispatch dispatch=nullptr;
    ~ComputePipelineState()
    {
        if (pipeline) { destroyPipeline(device,pipeline,nullptr); std::fprintf(stderr,"Vulkan: Compute pipeline destroyed.\n"); }
        if (layout) { destroyLayout(device,layout,nullptr); }
        if (setLayout) { destroySet(device,setLayout,nullptr); }
    }
};
struct ComputeBindingState
{
    std::shared_ptr<ComputePipelineState> pipeline;
    AccelerationRef acceleration;
    std::vector<BufferRef> buffers;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    VkDescriptorSet set=VK_NULL_HANDLE;
    PFN_vkDestroyDescriptorPool destroyPool=nullptr;
    ~ComputeBindingState()
    {
        if (pool) { destroyPool(pipeline->device,pool,nullptr); std::fprintf(stderr,"Vulkan: Compute binding destroyed.\n"); }
    }
};
ComputePipelineRef ComputePipelineAccess::create(const VulkanDevice& device, const ComputePipelineDesc& desc)
{
    const uint64_t count=uint64_t(desc.readBuffers)+desc.writeBuffers;
    const auto& limits=device.physicalDeviceInfo().properties.limits;
    if (!device.handle() || !count || count>limits.maxPerStageDescriptorStorageBuffers ||
        count>limits.maxDescriptorSetStorageBuffers || count+uint32_t(desc.accelerationStructure)>limits.maxPerStageResources)
    { std::fprintf(stderr,"Vulkan error: unsupported compute storage binding count.\n"); return {}; }
    if (desc.accelerationStructure && (!device.rayQueryEnabled() || !device.physicalDeviceInfo().accelerationStructureLimits ||
        !device.physicalDeviceInfo().accelerationStructureLimits->maxDescriptorSetAccelerationStructures ||
        !device.physicalDeviceInfo().accelerationStructureLimits->maxPerStageDescriptorAccelerationStructures))
    { std::fprintf(stderr,"Vulkan error: compute AS binding requires enabled Ray Query and AS descriptor limits.\n"); return {}; }
    VulkanShaderModule shader;
    if (!shader.initialize(device,desc.shader)) { return {}; }
    auto state=std::make_shared<ComputePipelineState>();
    state->device=device.handle(); state->reads=desc.readBuffers; state->writes=desc.writeBuffers;
    state->acceleration=desc.accelerationStructure;
    std::copy_n(limits.maxComputeWorkGroupCount,3,state->maxGroups.begin());
    const auto getProc=loadFunction<PFN_vkGetDeviceProcAddr>(device.physicalDeviceInfo().instance,"vkGetDeviceProcAddr");
    if (!getProc) { return {}; }
    const auto load=[&](auto& fn,const char* name) {
        fn=reinterpret_cast<std::remove_reference_t<decltype(fn)>>(getProc(device.handle(),name));
        if (!fn) { std::fprintf(stderr,"Vulkan error: compute pipeline requires %s.\n",name); }
        return fn!=nullptr;
    };
    PFN_vkCreateDescriptorSetLayout createSet=nullptr;
    PFN_vkCreatePipelineLayout createLayout=nullptr;
    PFN_vkCreateComputePipelines createPipeline=nullptr;
    if (!load(createSet,"vkCreateDescriptorSetLayout") || !load(state->destroySet,"vkDestroyDescriptorSetLayout") ||
        !load(createLayout,"vkCreatePipelineLayout") || !load(state->destroyLayout,"vkDestroyPipelineLayout") ||
        !load(createPipeline,"vkCreateComputePipelines") || !load(state->destroyPipeline,"vkDestroyPipeline") ||
        !load(state->bindPipeline,"vkCmdBindPipeline") || !load(state->bindSets,"vkCmdBindDescriptorSets") ||
        !load(state->dispatch,"vkCmdDispatch")) { return {}; }
    std::vector<VkDescriptorSetLayoutBinding> bindings(static_cast<size_t>(count)+desc.accelerationStructure);
    for (uint32_t i=0;i<count;++i) { bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}; }
    if (desc.accelerationStructure) { bindings.back()={static_cast<uint32_t>(count),VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}; }
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    set.bindingCount=static_cast<uint32_t>(bindings.size()); set.pBindings=bindings.data();
    if (!checkResult(createSet(device.handle(),&set,nullptr,&state->setLayout),"create compute set layout")) { return {}; }
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount=1; layout.pSetLayouts=&state->setLayout;
    if (!checkResult(createLayout(device.handle(),&layout,nullptr,&state->layout),"create compute pipeline layout")) { return {}; }
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; pipeline.stage.module=shader.handle(); pipeline.stage.pName="computeMain";
    pipeline.layout=state->layout;
    if (!checkResult(createPipeline(device.handle(),VK_NULL_HANDLE,1,&pipeline,nullptr,&state->pipeline),"create compute pipeline")) { return {}; }
    ComputePipelineRef result; result.state_=std::move(state);
    std::fprintf(stderr,"Vulkan: Compute pipeline created; %u read / %u write storage buffers.\n",desc.readBuffers,desc.writeBuffers);
    return result;
}
bool ComputePipelineAccess::counts(const ComputePipelineRef& pipeline,size_t reads,size_t writes,bool acceleration)
{ return pipeline.state_ && pipeline.state_->reads==reads && pipeline.state_->writes==writes && pipeline.state_->acceleration==acceleration; }
bool ComputePipelineAccess::matches(const ComputeBinding& binding,const ComputePipelineRef& pipeline,std::span<const BufferRef> buffers,const AccelerationRef& acceleration)
{
    if (!binding || binding->pipeline!=pipeline.state_ || binding->buffers.size()!=buffers.size()) { return false; }
    if (AccelerationAccess::handle(binding->acceleration)!=AccelerationAccess::handle(acceleration)) { return false; }
    for (size_t i=0;i<buffers.size();++i) {
        if (BufferAccess::handle(binding->buffers[i])!=BufferAccess::handle(buffers[i])) { return false; }
    }
    return true;
}
ComputeBinding ComputePipelineAccess::bind(const VulkanDevice& device,const ComputePipelineRef& pipeline,std::span<const BufferRef> buffers,const AccelerationRef& acceleration)
{
    if (!pipeline.state_ || pipeline.state_->device!=device.handle() ||
        uint64_t(pipeline.state_->reads)+pipeline.state_->writes!=buffers.size()) { return {}; }
    if (pipeline.state_->acceleration!=bool(acceleration) || (acceleration &&
        (!AccelerationAccess::isTop(acceleration) || AccelerationAccess::device(acceleration)!=device.handle()))) { return {}; }
    for (const auto& buffer:buffers) {
        if (BufferAccess::device(buffer)!=device.handle() || !(BufferAccess::usage(buffer)&VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
            !buffer.sizeBytes() || buffer.sizeBytes()>device.physicalDeviceInfo().properties.limits.maxStorageBufferRange) { return {}; }
    }
    auto binding=std::make_shared<ComputeBindingState>();
    binding->pipeline=pipeline.state_; binding->buffers.assign(buffers.begin(),buffers.end());
    binding->acceleration=acceleration;
    const auto getProc=loadFunction<PFN_vkGetDeviceProcAddr>(device.physicalDeviceInfo().instance,"vkGetDeviceProcAddr");
    if (!getProc) { return {}; }
    const auto createPool=reinterpret_cast<PFN_vkCreateDescriptorPool>(getProc(device.handle(),"vkCreateDescriptorPool"));
    const auto allocate=reinterpret_cast<PFN_vkAllocateDescriptorSets>(getProc(device.handle(),"vkAllocateDescriptorSets"));
    const auto update=reinterpret_cast<PFN_vkUpdateDescriptorSets>(getProc(device.handle(),"vkUpdateDescriptorSets"));
    binding->destroyPool=reinterpret_cast<PFN_vkDestroyDescriptorPool>(getProc(device.handle(),"vkDestroyDescriptorPool"));
    if (!createPool || !allocate || !update || !binding->destroyPool) { return {}; }
    const VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,static_cast<uint32_t>(buffers.size())},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets=1; pool.poolSizeCount=acceleration ? 2 : 1; pool.pPoolSizes=sizes;
    if (!checkResult(createPool(device.handle(),&pool,nullptr,&binding->pool),"create compute descriptor pool")) { return {}; }
    VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set.descriptorPool=binding->pool; set.descriptorSetCount=1; set.pSetLayouts=&pipeline.state_->setLayout;
    if (!checkResult(allocate(device.handle(),&set,&binding->set),"allocate compute descriptor set")) { return {}; }
    std::vector<VkDescriptorBufferInfo> infos(buffers.size());
    std::vector<VkWriteDescriptorSet> writes(buffers.size()+bool(acceleration));
    for (uint32_t i=0;i<buffers.size();++i) {
        infos[i]={BufferAccess::handle(buffers[i]),0,buffers[i].sizeBytes()};
        writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet=binding->set; writes[i].dstBinding=i; writes[i].descriptorCount=1;
        writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo=&infos[i];
    }
    const auto handle=AccelerationAccess::handle(acceleration);
    VkWriteDescriptorSetAccelerationStructureKHR as{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    if (acceleration) {
        as.accelerationStructureCount=1; as.pAccelerationStructures=&handle;
        auto& write=writes.back(); write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.pNext=&as; write.dstSet=binding->set; write.dstBinding=static_cast<uint32_t>(buffers.size());
        write.descriptorCount=1; write.descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    update(device.handle(),static_cast<uint32_t>(writes.size()),writes.data(),0,nullptr);
    std::fprintf(stderr,"Vulkan: Compute binding created.\n");
    return binding;
}
bool ComputePipelineAccess::dispatch(VkCommandBuffer command,const ComputeBinding& binding,uint32_t x,uint32_t y,uint32_t z)
{
    if (!command || !binding || !x || !y || !z) { return false; }
    const auto& p=*binding->pipeline;
    if (x>p.maxGroups[0] || y>p.maxGroups[1] || z>p.maxGroups[2]) { return false; }
    p.bindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline);
    p.bindSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,1,&binding->set,0,nullptr);
    p.dispatch(command,x,y,z); return true;
}
}
