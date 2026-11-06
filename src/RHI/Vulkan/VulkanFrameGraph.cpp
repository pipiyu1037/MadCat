#include "VulkanFrameGraph.h"
#include "VulkanBuffer.h"
#include "VulkanDepthTexture.h"
#include "VulkanDevice.h"
#include "VulkanFrameLighting.h"
#include "VulkanFunctions.h"
#include "VulkanSampledTexture.h"
#include "VulkanTextureState.h"
#include "VulkanTrianglePipeline.h"
#include <algorithm>
#include <type_traits>

namespace madcat::rhi::detail
{
namespace
{
template<class T> uint64_t identity(T handle)
{
    if constexpr (std::is_pointer_v<T>) { return reinterpret_cast<uintptr_t>(handle); }
    else { return static_cast<uint64_t>(handle); }
}
struct Scope { VkPipelineStageFlags2 stage=0; VkAccessFlags2 access=0; VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED; };
Scope scope(graph::Use use)
{
    using enum graph::Use;
    Scope result;
    if (graph::includes(use,HostWrite)) { result.stage|=VK_PIPELINE_STAGE_2_HOST_BIT; result.access|=VK_ACCESS_2_HOST_WRITE_BIT; }
    if (graph::includes(use,Vertex)) { result.stage|=VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT; result.access|=VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT; }
    if (graph::includes(use,Index)) { result.stage|=VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT; result.access|=VK_ACCESS_2_INDEX_READ_BIT; }
    if (graph::includes(use,Uniform)) { result.stage|=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT; result.access|=VK_ACCESS_2_UNIFORM_READ_BIT; }
    if (graph::includes(use,Sampled)) { result.stage|=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT; result.access|=VK_ACCESS_2_SHADER_SAMPLED_READ_BIT; result.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }
    if (graph::includes(use,Color)) { result.stage|=VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; result.access|=VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT; result.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }
    if (graph::includes(use,Depth)) { result.stage|=VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT; result.access|=VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; result.layout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
    if (graph::includes(use,Present)) { result.layout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; }
    return result;
}
}
struct VulkanFrameGraph::State : GraphicsCommands
{
    struct Native { VkImage image=VK_NULL_HANDLE; VkBuffer buffer=VK_NULL_HANDLE; VkImageAspectFlags aspect=0; uint32_t mips=1; };
    graph::GraphicsGraph program;
    std::vector<Native> native;
    std::vector<graph::Use> initial;
    uint32_t colorSlot=0, depthSlot=0, vertexSlot=UINT32_MAX, indexSlot=UINT32_MAX, lightingSlot=UINT32_MAX;
    std::vector<uint32_t> materialTextures;
    TextureRef depth;
    const VulkanTrianglePipeline* pipeline=nullptr;
    const VulkanFrameLighting* lighting=nullptr;
    VkImageView colorView=VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkCommandBuffer command=VK_NULL_HANDLE;
    PFN_vkCmdPipelineBarrier2 barrier=nullptr;
    PFN_vkCmdBeginRendering begin=nullptr;
    PFN_vkCmdEndRendering end=nullptr;

    bool transition(const graph::Barrier& value)
    {
        auto before=scope(value.before), after=scope(value.after);
        const auto& resource=native[value.resource];
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        if (resource.image)
        {
            // Swapchain transition must be in the acquire wait's stage even
            // when the prior layout is UNDEFINED and has no memory access.
            if (value.resource==colorSlot && (value.before==graph::Use::None || value.before==graph::Use::Present))
            { before.stage=VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; }
            VkImageMemoryBarrier2 image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            image.srcStageMask=before.stage; image.srcAccessMask=before.access;
            image.dstStageMask=after.stage; image.dstAccessMask=after.access;
            image.oldLayout=before.layout; image.newLayout=after.layout;
            image.srcQueueFamilyIndex=image.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            image.image=resource.image; image.subresourceRange={resource.aspect,0,resource.mips,0,1};
            dependency.imageMemoryBarrierCount=1; dependency.pImageMemoryBarriers=&image;
            barrier(command,&dependency);
        }
        else
        {
            VkBufferMemoryBarrier2 buffer{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
            buffer.srcStageMask=before.stage; buffer.srcAccessMask=before.access;
            buffer.dstStageMask=after.stage; buffer.dstAccessMask=after.access;
            buffer.srcQueueFamilyIndex=buffer.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            buffer.buffer=resource.buffer; buffer.size=VK_WHOLE_SIZE;
            dependency.bufferMemoryBarrierCount=1; dependency.pBufferMemoryBarriers=&buffer;
            barrier(command,&dependency);
        }
        return true;
    }
    bool beginRendering(uint32_t color, uint32_t depthIndex, AttachmentLoad load, const std::array<float,4>& clearColor) override
    {
        if (color!=colorSlot || depthIndex!=depthSlot) { return false; }
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView=colorView; attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp=load==AttachmentLoad::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        std::ranges::copy(clearColor,attachment.clearValue.color.float32);
        VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depthAttachment.imageView=TextureAccess::view(depth); depthAttachment.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp=attachment.loadOp; depthAttachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil={1,0};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent=extent; rendering.layerCount=1; rendering.colorAttachmentCount=1;
        rendering.pColorAttachments=&attachment; rendering.pDepthAttachment=&depthAttachment;
        begin(command,&rendering); return true;
    }
    bool drawIndexed(uint32_t vertices, uint32_t indices, uint32_t uniform,
        std::span<const uint32_t> textures, const IndexedDraw& draw) override
    {
        if (!pipeline || vertices!=vertexSlot || indices!=indexSlot || uniform!=lightingSlot) { return false; }
        if (!materialTextures.empty() && (draw.material>=materialTextures.size() ||
            std::ranges::find(textures,materialTextures[draw.material])==textures.end())) { return false; }
        return pipeline->recordIndexedDraw(command,extent,draw,lighting->set());
    }
    bool endRendering() override { end(command); return true; }
};
VulkanFrameGraph::VulkanFrameGraph()=default;
VulkanFrameGraph::~VulkanFrameGraph()=default;
bool VulkanFrameGraph::initialize(const VulkanDevice& device, VkImage image, VkImageView view, VkExtent2D extent,
    TextureRef depth, const VulkanTrianglePipeline* pipeline, const VulkanFrameLighting& lighting,
    const graph::GraphicsGraphFactory& factory)
{
    if (state_ || !device.handle() || !image || !view || !depth || !factory) { return false; }
    auto state=std::make_unique<State>();
    const auto getProc=loadFunction<PFN_vkGetDeviceProcAddr>(device.physicalDeviceInfo().instance,"vkGetDeviceProcAddr");
    if (!getProc) { return false; }
    state->barrier=reinterpret_cast<PFN_vkCmdPipelineBarrier2>(getProc(device.handle(),"vkCmdPipelineBarrier2"));
    state->begin=reinterpret_cast<PFN_vkCmdBeginRendering>(getProc(device.handle(),"vkCmdBeginRendering"));
    state->end=reinterpret_cast<PFN_vkCmdEndRendering>(getProc(device.handle(),"vkCmdEndRendering"));
    if (!state->barrier || !state->begin || !state->end) { return false; }
    state->depth=std::move(depth); state->pipeline=pipeline; state->lighting=&lighting; state->extent=extent; state->colorView=view;
    graph::Graph graph;
    const auto import=[&](std::string name, uint64_t id, graph::Kind kind, graph::Use use, bool valid, State::Native native) {
        auto resource=graph.import(std::move(name),id,kind,use,valid);
        if (resource.index==state->native.size()) { state->native.push_back(native); state->initial.push_back(use); }
        return resource;
    };
    const auto color=import("Swapchain",identity(image),graph::Kind::Color,graph::Use::None,false,{image,VK_NULL_HANDLE,VK_IMAGE_ASPECT_COLOR_BIT});
    const auto depthResource=import("Depth",identity(TextureAccess::image(state->depth)),graph::Kind::Depth,graph::Use::None,false,
        {TextureAccess::image(state->depth),VK_NULL_HANDLE,VK_IMAGE_ASPECT_DEPTH_BIT});
    state->colorSlot=color.index; state->depthSlot=depthResource.index;
    graph::GraphicsImports input{color,depthResource,{},{},{},{}};
    if (pipeline)
    {
        const auto vertexBuffer=BufferAccess::handle(pipeline->vertexBuffer()), indexBuffer=BufferAccess::handle(pipeline->indexBuffer());
        input.vertices=import("Vertices",identity(vertexBuffer),graph::Kind::Buffer,graph::Use::HostWrite,true,{VK_NULL_HANDLE,vertexBuffer});
        input.indices=import("Indices",identity(indexBuffer),graph::Kind::Buffer,graph::Use::HostWrite,true,{VK_NULL_HANDLE,indexBuffer});
        state->vertexSlot=input.vertices.index; state->indexSlot=input.indices.index;
        if (!pipeline->materialBindings().empty())
        {
            input.lighting=import("FrameLighting",identity(lighting.buffer()),graph::Kind::Buffer,graph::Use::HostWrite,true,{VK_NULL_HANDLE,lighting.buffer()});
            state->lightingSlot=input.lighting.index;
        }
        for (const auto& material : pipeline->materialBindings())
        {
            auto texture=TextureBindingAccess::texture(material);
            auto resource=import("BaseColor",identity(TextureAccess::image(texture)),graph::Kind::Color,graph::Use::Sampled,true,
                {TextureAccess::image(texture),VK_NULL_HANDLE,VK_IMAGE_ASPECT_COLOR_BIT,TextureAccess::mipLevels(texture)});
            state->materialTextures.push_back(resource.index);
            if (std::ranges::find(input.textures,resource)==input.textures.end()) { input.textures.push_back(resource); }
        }
    }
    auto program=factory(graph,input);
    if (!program) { std::fprintf(stderr,"Render Graph error: %s\n",program.error().c_str()); return false; }
    if (!program->parameters || program->plan.resources().size()!=state->native.size())
    { std::fprintf(stderr,"Render Graph error: graphics executor supports imported resources only and requires frame parameters.\n"); return false; }
    for (uint32_t slot=0; slot<state->native.size(); ++slot)
    {
        const auto& native=state->native[slot];
        const auto expected=native.image ? identity(native.image) : identity(native.buffer);
        if (program->plan.resources()[slot].identity!=expected)
        { std::fprintf(stderr,"Render Graph error: plan/import identity mismatch.\n"); return false; }
    }
    for (const auto& node : program->plan.nodes())
    {
        for (const auto& access : node.accesses)
        {
            const auto slot=access.resource.index;
            const auto allowed=slot==state->colorSlot ? graph::Use::Color|graph::Use::Present :
                slot==state->depthSlot ? graph::Use::Depth : slot==state->vertexSlot ? graph::Use::Vertex :
                slot==state->indexSlot ? graph::Use::Index : slot==state->lightingSlot ? graph::Use::Uniform : graph::Use::Sampled;
            if (!graph::includes(allowed,access.use))
            { std::fprintf(stderr,"Render Graph error: %s requests an unsupported resource usage.\n",node.name.c_str()); return false; }
        }
    }
    state->program=std::move(*program);
    std::string order;
    for (const auto node : state->program.plan.order()) { if (!order.empty()) { order+=" -> "; } order+=state->program.plan.nodes()[node].name; }
    std::fprintf(stderr,"Render Graph: compiled %zu Passes, %zu imported resources; %s.\n",state->program.plan.nodes().size(),state->native.size(),order.c_str());
    state_=std::move(state); return true;
}
bool VulkanFrameGraph::record(VkCommandBuffer command, const std::array<float,4>& color,
    std::span<const IndexedDraw> draws, bool presented, bool depthInitialized)
{
    if (!state_ || !command) { return false; }
    auto& state=*state_; state.command=command;
    auto initial=state.initial;
    initial[state.colorSlot]=presented ? graph::Use::Present : graph::Use::None;
    initial[state.depthSlot]=depthInitialized ? graph::Use::Depth : graph::Use::None;
    if (state.vertexSlot!=UINT32_MAX && depthInitialized)
    { initial[state.vertexSlot]=graph::Use::Vertex; initial[state.indexSlot]=graph::Use::Index; }
    const auto parameters=state.program.parameters(color,draws);
    const auto result=state.program.plan.record(state,parameters,initial,[&](const auto& barrier){ return state.transition(barrier); });
    if (!result) { std::fprintf(stderr,"Render Graph error: %s\n",result.error().c_str()); return false; }
    if ((*result)[state.colorSlot]!=graph::Use::Present || (*result)[state.depthSlot]!=graph::Use::Depth)
    { std::fprintf(stderr,"Render Graph error: output states do not match presenter/depth handoff.\n"); return false; }
    return true;
}
}
