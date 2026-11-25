#include "VulkanFrameGraph.h"
#include "VulkanBuffer.h"
#include "VulkanDepthTexture.h"
#include "VulkanDepthViewPipeline.h"
#include "VulkanDevice.h"
#include "VulkanFrameLighting.h"
#include "VulkanFunctions.h"
#include "VulkanSampledTexture.h"
#include "VulkanTextureState.h"
#include "VulkanTrianglePipeline.h"
#include "VulkanGraphicsPipeline.h"
#include "VulkanUi.h"
#include <algorithm>
#include <array>
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
    if (graph::includes(use,Uniform)) { result.stage|=VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT; result.access|=VK_ACCESS_2_UNIFORM_READ_BIT; }
    if (graph::includes(use,Sampled)) { result.stage|=VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT; result.access|=VK_ACCESS_2_SHADER_SAMPLED_READ_BIT; result.layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }
    if (graph::includes(use,Color)) { result.stage|=VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT; result.access|=VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT; result.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }
    if (graph::includes(use,Depth)) { result.stage|=VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT; result.access|=VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; result.layout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
    if (graph::includes(use,Present)) { result.layout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; }
    return result;
}
}
struct VulkanFrameGraph::State : GraphicsCommands
{
    struct Native { VkImage image=VK_NULL_HANDLE; VkBuffer buffer=VK_NULL_HANDLE; VkImageAspectFlags aspect=0; uint32_t mips=1;
        TextureRef ownedTexture; TextureBindingRef sampled; };
    graph::GraphicsGraph program;
    std::vector<Native> native;
    std::vector<graph::Use> initial;
    std::vector<graph::Use> pending;
    bool geometrySubmitted = false;
    uint32_t colorSlot=0, depthSlot=0, vertexSlot=UINT32_MAX, indexSlot=UINT32_MAX, lightingSlot=UINT32_MAX;
    std::vector<std::array<uint32_t,3>> materialTextures;
    TextureRef depth;
    const VulkanTrianglePipeline* pipeline=nullptr;
    const VulkanTrianglePipeline* shadow=nullptr;
    const VulkanDepthViewPipeline* depthView=nullptr;
    VulkanUi* ui=nullptr;
    uint32_t uiFontSlot=UINT32_MAX;
    bool uiRecorded=false;
    uint32_t activeDepth=UINT32_MAX;
    VkDevice device=VK_NULL_HANDLE;
    VkFormat presentationFormat=VK_FORMAT_UNDEFINED, activeColorFormat=VK_FORMAT_UNDEFINED;
    GraphicsPipelineRef activePipeline;
    bool textureBound=false, constantsSet=false;
    bool depthOnly=false;
    bool shadowsEnabled=false;
    VkExtent2D renderExtent{};
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
        if ((color!=UINT32_MAX && (color>=native.size() || native[color].aspect!=VK_IMAGE_ASPECT_COLOR_BIT ||
            (color!=colorSlot && !native[color].ownedTexture))) || (depthIndex!=UINT32_MAX &&
            (depthIndex>=native.size() || native[depthIndex].aspect!=VK_IMAGE_ASPECT_DEPTH_BIT))) { return false; }
        depthOnly=color==UINT32_MAX;
        activeDepth=depthIndex;
        activePipeline={}; textureBound=false; constantsSet=false;
        const auto colorTarget=color==UINT32_MAX || color==colorSlot ? TextureRef{} : native[color].ownedTexture;
        activeColorFormat=depthOnly ? VK_FORMAT_UNDEFINED : color==colorSlot ? presentationFormat : TextureAccess::format(colorTarget);
        VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        attachment.imageView=color==colorSlot ? colorView : TextureAccess::view(colorTarget); attachment.imageLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp=load==AttachmentLoad::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        std::ranges::copy(clearColor,attachment.clearValue.color.float32);
        VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        const auto depthTarget=depthIndex==depthSlot ? depth : depthIndex==UINT32_MAX ? TextureRef{} : native[depthIndex].ownedTexture;
        renderExtent=depthOnly ? VkExtent2D{depthTarget.width(),depthTarget.height()} :
            color==colorSlot ? extent : VkExtent2D{colorTarget.width(),colorTarget.height()};
        if ((depthOnly && !depthTarget) || (depthTarget &&
            (depthTarget.width()!=renderExtent.width || depthTarget.height()!=renderExtent.height))) { return false; }
        depthAttachment.imageView=TextureAccess::view(depthTarget); depthAttachment.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp=attachment.loadOp; depthAttachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil={1,0};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent=renderExtent; rendering.layerCount=1; rendering.colorAttachmentCount=depthOnly ? 0 : 1;
        rendering.pColorAttachments=depthOnly ? nullptr : &attachment; rendering.pDepthAttachment=depthIndex==UINT32_MAX ? nullptr : &depthAttachment;
        begin(command,&rendering); return true;
    }
    bool drawIndexed(uint32_t vertices, uint32_t indices, uint32_t uniform,
        std::span<const uint32_t> textures, const IndexedDraw& draw, uint32_t shadowSlot) override
    {
        const auto* selected=depthOnly ? shadow : pipeline;
        if (!selected || activeDepth==UINT32_MAX || vertices!=vertexSlot || indices!=indexSlot ||
            uniform!=(depthOnly ? UINT32_MAX : lightingSlot)) { return false; }
        const auto depthTarget=activeDepth==depthSlot ? depth : native[activeDepth].ownedTexture;
        if (selected->colorFormat()!=activeColorFormat || selected->depthFormat()!=TextureAccess::format(depthTarget)) { return false; }
        activePipeline={};
        if (!materialTextures.empty())
        {
            if (draw.material>=materialTextures.size()) { return false; }
            const auto& slots=materialTextures[draw.material];
            if (std::ranges::find(textures,slots[0])==textures.end() ||
                (!depthOnly && (std::ranges::find(textures,slots[1])==textures.end() ||
                    std::ranges::find(textures,slots[2])==textures.end()))) { return false; }
        }
        TextureBindingRef binding;
        if (shadowSlot!=UINT32_MAX)
        {
            if (depthOnly || shadowSlot>=native.size() || native[shadowSlot].aspect!=VK_IMAGE_ASPECT_DEPTH_BIT || !native[shadowSlot].sampled) { return false; }
            binding=native[shadowSlot].sampled;
        }
        if (!depthOnly && shadowsEnabled && !binding) { return false; }
        return selected->recordIndexedDraw(command,renderExtent,draw,depthOnly ? VK_NULL_HANDLE : lighting->set(),binding);
    }
    bool endRendering() override { end(command); activePipeline={}; return true; }
    bool displayDepth(uint32_t slot) override
    {
        activePipeline={};
        return depthView && depthView->colorFormat()==activeColorFormat && !depthOnly && activeDepth==UINT32_MAX &&
            slot<native.size() && native[slot].aspect==VK_IMAGE_ASPECT_DEPTH_BIT && native[slot].sampled &&
            depthView->record(command,renderExtent,native[slot].sampled);
    }
    bool bindPipeline(const GraphicsPipelineRef& value) override
    {
        activePipeline={}; textureBound=false; constantsSet=false;
        if (depthOnly || activeDepth!=UINT32_MAX || GraphicsPipelineAccess::device(value)!=device ||
            GraphicsPipelineAccess::format(value)!=activeColorFormat || !GraphicsPipelineAccess::bind(value,command,renderExtent)) { return false; }
        activePipeline=value; return true;
    }
    bool bindSampledTexture(uint32_t set, uint32_t slot) override
    {
        if (set || slot>=native.size() || !GraphicsPipelineAccess::bindTexture(activePipeline,command,native[slot].sampled)) { return false; }
        textureBound=true; return true;
    }
    bool pushConstants(std::span<const std::byte> bytes) override
    {
        if (!GraphicsPipelineAccess::push(activePipeline,command,bytes)) { return false; }
        constantsSet=true; return true;
    }
    bool draw(uint32_t count) override
    {
        return activePipeline && (!GraphicsPipelineAccess::samplesTexture(activePipeline) || textureBound) &&
            (!GraphicsPipelineAccess::constantBytes(activePipeline) || constantsSet) && GraphicsPipelineAccess::draw(activePipeline,command,count);
    }
    bool drawUi(uint32_t fontSlot) override
    {
        activePipeline={};
        if (!ui || uiRecorded || fontSlot!=uiFontSlot || depthOnly || activeDepth!=UINT32_MAX ||
            !ui->record(command,activeColorFormat)) { return false; }
        uiRecorded=true; return true;
    }
};
VulkanFrameGraph::VulkanFrameGraph()=default;
VulkanFrameGraph::~VulkanFrameGraph()=default;
bool VulkanFrameGraph::initialize(const VulkanDevice& device, VkImage image, VkImageView view, VkExtent2D extent,
    TextureRef depth, const VulkanTrianglePipeline* pipeline, const VulkanFrameLighting& lighting,
    const graph::GraphicsGraphFactory& factory, const VulkanDepthViewPipeline* depthView, const VulkanTrianglePipeline* shadow, VkFormat presentationFormat, VulkanUi* ui)
{
    if (state_ || !device.handle() || !image || !view || !depth || !factory) { return false; }
    if (depthView && (depthView->deviceHandle()!=device.handle() ||
        depthView->colorFormat()!=presentationFormat)) { return false; }
    if (shadow && (!pipeline || !shadow->handle() || shadow->deviceHandle()!=device.handle() ||
        shadow->colorFormat()!=VK_FORMAT_UNDEFINED || shadow->depthFormat()!=pipeline->depthFormat() ||
        BufferAccess::handle(shadow->vertexBuffer())!=BufferAccess::handle(pipeline->vertexBuffer()) ||
        BufferAccess::handle(shadow->indexBuffer())!=BufferAccess::handle(pipeline->indexBuffer()) ||
        shadow->materialBindings().size()!=pipeline->materialBindings().size())) { return false; }
    if (shadow) { for (size_t i=0; i<shadow->materialBindings().size(); ++i) {
        if (TextureBindingAccess::set(shadow->materialBindings()[i].baseColor)!=TextureBindingAccess::set(pipeline->materialBindings()[i].baseColor) ||
            TextureBindingAccess::set(shadow->materialBindings()[i].data)!=TextureBindingAccess::set(pipeline->materialBindings()[i].data)) { return false; }
    } }
    auto state=std::make_unique<State>();
    state->device=device.handle(); state->presentationFormat=presentationFormat;
    const auto getProc=loadFunction<PFN_vkGetDeviceProcAddr>(device.physicalDeviceInfo().instance,"vkGetDeviceProcAddr");
    if (!getProc) { return false; }
    state->barrier=reinterpret_cast<PFN_vkCmdPipelineBarrier2>(getProc(device.handle(),"vkCmdPipelineBarrier2"));
    state->begin=reinterpret_cast<PFN_vkCmdBeginRendering>(getProc(device.handle(),"vkCmdBeginRendering"));
    state->end=reinterpret_cast<PFN_vkCmdEndRendering>(getProc(device.handle(),"vkCmdEndRendering"));
    if (!state->barrier || !state->begin || !state->end) { return false; }
    state->depth=std::move(depth); state->pipeline=pipeline; state->lighting=&lighting; state->extent=extent; state->colorView=view;
    state->depthView=depthView; state->shadow=shadow;
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
    input.width=extent.width; input.height=extent.height;
    input.prepareGraphics=[&](const GraphicsPipelineDesc& desc) { return GraphicsPipelineAccess::create(device,presentationFormat,desc); };
    input.presentationSrgb=presentationFormat==VK_FORMAT_B8G8R8A8_SRGB || presentationFormat==VK_FORMAT_R8G8B8A8_SRGB;
    if (ui)
    {
        auto font=ui->font();
        if (!font || TextureAccess::device(font)!=device.handle()) { return false; }
        input.uiFont=import("UiFont",identity(TextureAccess::image(font)),graph::Kind::Color,graph::Use::Sampled,true,
            {TextureAccess::image(font),VK_NULL_HANDLE,VK_IMAGE_ASPECT_COLOR_BIT,TextureAccess::mipLevels(font),font});
        state->ui=ui; state->uiFontSlot=input.uiFont.index;
    }
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
            std::array<uint32_t,3> slots{};
            const TextureRef textures[]{TextureBindingAccess::texture(material.baseColor),TextureBindingAccess::texture(material.data),
                TextureBindingAccess::normalTexture(material.data)};
            const char* names[]{"BaseColor","MetallicRoughness","Normal"};
            for (size_t index=0; index<3; ++index)
            {
                const auto& texture=textures[index];
                auto resource=import(names[index],identity(TextureAccess::image(texture)),graph::Kind::Color,graph::Use::Sampled,true,
                    {TextureAccess::image(texture),VK_NULL_HANDLE,VK_IMAGE_ASPECT_COLOR_BIT,TextureAccess::mipLevels(texture)});
                slots[index]=resource.index;
                if (std::ranges::find(input.textures,resource)==input.textures.end()) { input.textures.push_back(resource); }
            }
            state->materialTextures.push_back(slots);
        }
    }
    auto program=factory(graph,input);
    if (!program) { std::fprintf(stderr,"Render Graph error: %s\n",program.error().c_str()); return false; }
    if (!program->parameters || program->plan.resources().size()<state->native.size())
    { std::fprintf(stderr,"Render Graph error: invalid resource table or missing frame parameters.\n"); return false; }
    const auto importedCount=state->native.size();
    for (size_t slot=importedCount; slot<program->plan.resources().size(); ++slot)
    {
        const auto& resource=program->plan.resources()[slot];
        if (!resource.depthTexture && !resource.colorTexture) { std::fprintf(stderr,"Render Graph error: unsupported managed resource.\n"); return false; }
        const bool isDepth=resource.depthTexture.has_value();
        const auto width=isDepth ? resource.depthTexture->width : resource.colorTexture->width;
        const auto height=isDepth ? resource.depthTexture->height : resource.colorTexture->height;
        const auto sampled=isDepth ? resource.depthTexture->sampled : resource.colorTexture->sampled;
        auto texture=isDepth ? createDepthTexture(device,width,height,sampled) : createColorTexture(device,width,height,sampled);
        if (!texture) { return false; }
        if (isDepth && pipeline && TextureAccess::format(texture)!=pipeline->depthFormat())
        { std::fprintf(stderr,"Render Graph error: sampled depth format is incompatible with the prepared forward pipeline.\n"); return false; }
        TextureBindingRef binding;
        if (sampled)
        {
            SamplerDesc sampler; sampler.linearMag=sampler.linearMin=false;
            sampler.wrapU=sampler.wrapV=SamplerDesc::Wrap::Clamp; sampler.mips=SamplerDesc::Mips::None;
            binding=createTextureBinding(device,texture,sampler);
            if (!binding) { return false; }
        }
        state->native.push_back({TextureAccess::image(texture),VK_NULL_HANDLE,VkImageAspectFlags(isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),1,texture,binding});
        state->initial.push_back(graph::Use::None);
    }
    for (uint32_t slot=0; slot<state->native.size(); ++slot)
    {
        const auto& native=state->native[slot];
        const auto expected=native.image ? identity(native.image) : identity(native.buffer);
        if (slot<importedCount && program->plan.resources()[slot].identity!=expected)
        { std::fprintf(stderr,"Render Graph error: plan/import identity mismatch.\n"); return false; }
    }
    for (const auto& node : program->plan.nodes())
    {
        for (const auto& access : node.accesses)
        {
            const auto slot=access.resource.index;
            const auto allowed=slot>=importedCount ? (state->native[slot].aspect==VK_IMAGE_ASPECT_DEPTH_BIT ? graph::Use::Depth : graph::Use::Color)|(state->native[slot].sampled ? graph::Use::Sampled : graph::Use::None) :
                slot==state->colorSlot ? graph::Use::Color|graph::Use::Present :
                slot==state->depthSlot ? graph::Use::Depth : slot==state->vertexSlot ? graph::Use::Vertex :
                slot==state->indexSlot ? graph::Use::Index : slot==state->lightingSlot ? graph::Use::Uniform : graph::Use::Sampled;
            if (!graph::includes(allowed,access.use))
            { std::fprintf(stderr,"Render Graph error: %s requests an unsupported resource usage.\n",node.name.c_str()); return false; }
        }
    }
    auto finalColor=graph::Use::None;
    for (const auto node : program->plan.order()) { for (const auto& access : program->plan.nodes()[node].accesses) {
        if (access.resource.index==state->colorSlot) { finalColor=access.use; }
    } }
    if (finalColor!=graph::Use::Present)
    { std::fprintf(stderr,"Render Graph error: output must end in Present before publication.\n"); return false; }
    state->program=std::move(*program);
    std::string order;
    for (const auto node : state->program.plan.order()) { if (!order.empty()) { order+=" -> "; } order+=state->program.plan.nodes()[node].name; }
    std::fprintf(stderr,"Render Graph: compiled %zu Passes, %zu imported resources; %s.\n",state->program.plan.nodes().size(),importedCount,order.c_str());
    size_t managedDepth=0,managedColor=0;
    for (size_t slot=importedCount; slot<state->native.size(); ++slot)
    { if (state->native[slot].aspect==VK_IMAGE_ASPECT_DEPTH_BIT) { ++managedDepth; } else { ++managedColor; } }
    if (managedDepth) { std::fprintf(stderr,"Render Graph: allocated %zu managed depth texture(s).\n",managedDepth); }
    if (managedColor) { std::fprintf(stderr,"Render Graph: allocated %zu managed color texture(s).\n",managedColor); }
    state_=std::move(state); return true;
}
bool VulkanFrameGraph::record(VkCommandBuffer command, const std::array<float,4>& color,
    std::span<const IndexedDraw> draws, bool presented, bool depthInitialized,
    std::span<const IndexedDraw> shadowDraws, bool shadowsEnabled)
{
    if (!state_ || !command) { return false; }
    auto& state=*state_; state.command=command; state.shadowsEnabled=shadowsEnabled;
    state.uiRecorded=false;
    state.pending.clear();
    auto initial=state.initial;
    initial[state.colorSlot]=presented ? graph::Use::Present : graph::Use::None;
    initial[state.depthSlot]=depthInitialized ? graph::Use::Depth : graph::Use::None;
    if (state.vertexSlot!=UINT32_MAX && (depthInitialized || state.geometrySubmitted))
    { initial[state.vertexSlot]=graph::Use::Vertex; initial[state.indexSlot]=graph::Use::Index; }
    const auto parameters=state.program.parameters(color,draws,shadowDraws);
    const auto result=state.program.plan.record(state,parameters,initial,[&](const auto& barrier){ return state.transition(barrier); });
    if (!result) { std::fprintf(stderr,"Render Graph error: %s\n",result.error().c_str()); return false; }
    if (state.ui && !state.uiRecorded)
    { std::fprintf(stderr,"Render Graph error: enabled UI requires one UI drawing node.\n"); return false; }
    if ((*result)[state.colorSlot]!=graph::Use::Present ||
        ((*result)[state.depthSlot]!=graph::Use::Depth && (*result)[state.depthSlot]!=initial[state.depthSlot]))
    { std::fprintf(stderr,"Render Graph error: output states do not match presenter/depth handoff.\n"); return false; }
    state.pending=*result;
    return true;
}
bool VulkanFrameGraph::submitted()
{
    if (!state_ || state_->pending.empty()) { return false; }
    const bool depthInitialized=state_->pending[state_->depthSlot]==graph::Use::Depth;
    state_->geometrySubmitted=true;
    for (size_t slot=0; slot<state_->native.size(); ++slot)
    { if (state_->native[slot].ownedTexture) { state_->initial[slot]=state_->pending[slot]; } }
    state_->pending.clear();
    return depthInitialized;
}
}
