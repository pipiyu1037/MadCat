#include "VulkanSampledTexture.h"
#include "VulkanTextureState.h"
#include "VulkanDevice.h"
#include "VulkanFunctions.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <type_traits>

namespace madcat::rhi
{
namespace
{
template<class Function, class Info, class Handle>
bool createOwned(VkDevice device, Function function, const Info& info, Handle& output, const char* operation)
{
    Handle candidate = VK_NULL_HANDLE;
    if (!detail::checkResult(function(device, &info, nullptr, &candidate), operation)) { return false; }
    output = candidate;
    return true;
}

// Upload owns command storage and coherent staging until its fence completes.
struct Upload
{
    VkDevice device{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkCommandPool pool{};
    VkFence fence{};
    bool pending = false;
    PFN_vkDestroyBuffer destroyBuffer{};
    PFN_vkFreeMemory freeMemory{};
    PFN_vkDestroyCommandPool destroyPool{};
    PFN_vkDestroyFence destroyFence{};
    PFN_vkDeviceWaitIdle waitIdle{};
    ~Upload()
    {
        if (pending) { (void)waitIdle(device); }
        if (fence) { destroyFence(device, fence, nullptr); }
        if (pool) { destroyPool(device, pool, nullptr); }
        if (buffer) { destroyBuffer(device, buffer, nullptr); }
        if (memory) { freeMemory(device, memory, nullptr); }
    }
};
}

TextureRef createSampledTexture(const VulkanDevice& device, uint32_t width, uint32_t height, std::span<const uint8_t> rgba)
{
    if (!device.handle() || !width || !height || uint64_t(width) * height * 4 != rgba.size()) { return {}; }
    const auto instance = device.physicalDeviceInfo().instance;
    const auto getProc = detail::loadFunction<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
    const auto getFormats = detail::loadFunction<PFN_vkGetPhysicalDeviceFormatProperties>(instance, "vkGetPhysicalDeviceFormatProperties");
    const auto getImageFormats = detail::loadFunction<PFN_vkGetPhysicalDeviceImageFormatProperties>(instance, "vkGetPhysicalDeviceImageFormatProperties");
    const auto getMemory = detail::loadFunction<PFN_vkGetPhysicalDeviceMemoryProperties>(instance, "vkGetPhysicalDeviceMemoryProperties");
    if (!getProc || !getFormats || !getImageFormats || !getMemory) { return {}; }
    auto state = std::make_shared<detail::TextureState>();
    state->device = device.handle(); state->width = width; state->height = height;
    state->sampled = true; state->format = VK_FORMAT_R8G8B8A8_SRGB;
    state->mipLevels = std::bit_width(std::max(width, height));
    VkFormatProperties format{}; getFormats(device.physicalDevice(), state->format, &format);
    constexpr auto features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    VkImageFormatProperties imageLimits{};
    constexpr auto usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((format.optimalTilingFeatures & features) != features || !detail::checkResult(getImageFormats(device.physicalDevice(),
        state->format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &imageLimits), "sampled image support") ||
        width > imageLimits.maxExtent.width || height > imageLimits.maxExtent.height || state->mipLevels > imageLimits.maxMipLevels ||
        imageLimits.maxExtent.depth < 1 || imageLimits.maxArrayLayers < 1 || !(imageLimits.sampleCounts & VK_SAMPLE_COUNT_1_BIT))
    {
        std::fprintf(stderr, "Vulkan error: RGBA8 sRGB sampled image/mipmap extent or features unsupported.\n"); return {};
    }
    Upload upload; upload.device = device.handle();
    const auto load = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(getProc(device.handle(), name));
        if (!function) { std::fprintf(stderr, "Vulkan error: sampled image function %s is unavailable.\n", name); }
        return function != nullptr;
    };
#define LOAD(type, name) PFN_vk##type name{}; if (!load(name, "vk" #type)) { return {}; }
    LOAD(CreateImage, createImage) LOAD(GetImageMemoryRequirements, imageRequirements)
    LOAD(AllocateMemory, allocate) LOAD(BindImageMemory, bindImage) LOAD(CreateImageView, createView)
    LOAD(CreateBuffer, createBuffer) LOAD(GetBufferMemoryRequirements, bufferRequirements)
    LOAD(BindBufferMemory, bindBuffer) LOAD(MapMemory, map) LOAD(UnmapMemory, unmap)
    LOAD(CreateCommandPool, createPool) LOAD(AllocateCommandBuffers, allocateCommands)
    LOAD(BeginCommandBuffer, begin) LOAD(EndCommandBuffer, end) LOAD(CmdPipelineBarrier2, barrier)
    LOAD(CmdCopyBufferToImage, copy) LOAD(CmdBlitImage, blit) LOAD(CreateFence, createFence)
    LOAD(QueueSubmit2, submit) LOAD(WaitForFences, wait)
#undef LOAD
    if (!load(state->destroyImage, "vkDestroyImage") || !load(state->destroyView, "vkDestroyImageView") ||
        !load(state->freeMemory, "vkFreeMemory") || !load(upload.destroyBuffer, "vkDestroyBuffer") ||
        !load(upload.freeMemory, "vkFreeMemory") || !load(upload.destroyPool, "vkDestroyCommandPool") ||
        !load(upload.destroyFence, "vkDestroyFence") || !load(upload.waitIdle, "vkDeviceWaitIdle")) { return {}; }
    VkPhysicalDeviceMemoryProperties memory{}; getMemory(device.physicalDevice(), &memory);
    const auto allocateFor = [&](VkMemoryRequirements requirements, VkMemoryPropertyFlags flags, VkDeviceMemory& target, VkImage image, VkBuffer buffer) {
        for (uint32_t type = 0; type < memory.memoryTypeCount; ++type)
        {
            if ((requirements.memoryTypeBits & (1u << type)) && (memory.memoryTypes[type].propertyFlags & flags) == flags)
            {
                VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO}; dedicated.image = image; dedicated.buffer = buffer;
                VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; info.pNext = &dedicated;
                info.allocationSize = requirements.size; info.memoryTypeIndex = type;
                return createOwned(device.handle(), allocate, info, target, "sampled image memory");
            }
        }
        std::fprintf(stderr, "Vulkan error: sampled image/upload has no compatible memory type.\n"); return false;
    };
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D; imageInfo.format = state->format; imageInfo.extent = {width,height,1};
    imageInfo.mipLevels = state->mipLevels; imageInfo.arrayLayers = 1; imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; imageInfo.usage = usage; imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!createOwned(device.handle(), createImage, imageInfo, state->image, "create sampled image")) { return {}; }
    VkMemoryRequirements requirements{}; imageRequirements(device.handle(), state->image, &requirements);
    if (!allocateFor(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, state->memory, state->image, {}) ||
        !detail::checkResult(bindImage(device.handle(), state->image, state->memory, 0), "bind sampled image")) { return {}; }
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = state->image; viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; viewInfo.format = state->format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,state->mipLevels,0,1};
    if (!createOwned(device.handle(), createView, viewInfo, state->view, "sampled image view")) { return {}; }
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bufferInfo.size = rgba.size();
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!createOwned(device.handle(), createBuffer, bufferInfo, upload.buffer, "create image staging")) { return {}; }
    bufferRequirements(device.handle(), upload.buffer, &requirements);
    if (!allocateFor(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, upload.memory, {}, upload.buffer) ||
        !detail::checkResult(bindBuffer(device.handle(), upload.buffer, upload.memory, 0), "bind image staging")) { return {}; }
    void* mapped{};
    if (!detail::checkResult(map(device.handle(), upload.memory, 0, rgba.size(), 0, &mapped), "map image staging")) { return {}; }
    std::memcpy(mapped, rgba.data(), rgba.size()); unmap(device.handle(), upload.memory);
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.queueFamilyIndex = device.queueFamilyIndex();
    if (!createOwned(device.handle(), createPool, poolInfo, upload.pool, "image upload pool")) { return {}; }
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commands.commandPool = upload.pool; commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commands.commandBufferCount = 1;
    VkCommandBuffer command{};
    if (!detail::checkResult(allocateCommands(device.handle(), &commands, &command), "image upload command")) { return {}; }
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!detail::checkResult(begin(command, &beginInfo), "begin image upload")) { return {}; }
    const auto transition = [&](uint32_t first, uint32_t count, VkImageLayout oldLayout, VkImageLayout newLayout,
        VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier2 imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        imageBarrier.srcStageMask = srcStage; imageBarrier.srcAccessMask = srcAccess; imageBarrier.dstStageMask = dstStage; imageBarrier.dstAccessMask = dstAccess;
        imageBarrier.oldLayout = oldLayout; imageBarrier.newLayout = newLayout;
        imageBarrier.srcQueueFamilyIndex = imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = state->image; imageBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,first,count,0,1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO}; dependency.imageMemoryBarrierCount = 1; dependency.pImageMemoryBarriers = &imageBarrier;
        barrier(command, &dependency);
    };
    transition(0,state->mipLevels,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE,0,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy region{}; region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; region.imageExtent = {width,height,1};
    copy(command, upload.buffer, state->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    uint32_t w = width, h = height;
    for (uint32_t level = 0; level < state->mipLevels; ++level)
    {
        transition(level,1,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT);
        if (level + 1 == state->mipLevels) { break; }
        const auto nextW = std::max(1u,w/2), nextH = std::max(1u,h/2);
        VkImageBlit mip{}; mip.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,level,0,1}; mip.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,level+1,0,1};
        mip.srcOffsets[1] = {static_cast<int32_t>(w),static_cast<int32_t>(h),1}; mip.dstOffsets[1] = {static_cast<int32_t>(nextW),static_cast<int32_t>(nextH),1};
        blit(command,state->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,state->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&mip,VK_FILTER_LINEAR);
        w = nextW; h = nextH;
    }
    transition(0,state->mipLevels,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    if (!detail::checkResult(end(command), "end image upload")) { return {}; }
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!createOwned(device.handle(), createFence, fenceInfo, upload.fence, "image upload fence")) { return {}; }
    VkCommandBufferSubmitInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}; commandInfo.commandBuffer = command;
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2}; submitInfo.commandBufferInfoCount = 1; submitInfo.pCommandBufferInfos = &commandInfo;
    if (!detail::checkResult(submit(device.queue(),1,&submitInfo,upload.fence), "submit image upload")) { return {}; }
    upload.pending = true;
    if (!detail::checkResult(wait(device.handle(),1,&upload.fence,VK_TRUE,UINT64_MAX), "wait image upload")) { return {}; }
    upload.pending = false;
    std::fprintf(stderr, "Vulkan: Sampled texture uploaded; %u x %u, %u mip levels, sRGB, shader-read-only.\n",width,height,state->mipLevels);
    TextureRef result; result.state_ = std::move(state); return result;
}

struct detail::TextureBindingState
{
    TextureRef texture;
    VkDevice device{};
    VkDescriptorSetLayout layout{};
    VkDescriptorPool pool{};
    VkDescriptorSet set{};
    VkSampler sampler{};
    PFN_vkDestroyDescriptorPool destroyPool{};
    PFN_vkDestroyDescriptorSetLayout destroyLayout{};
    PFN_vkDestroySampler destroySampler{};
    ~TextureBindingState()
    {
        if (pool) { destroyPool(device,pool,nullptr); }
        if (layout) { destroyLayout(device,layout,nullptr); }
        if (sampler) { destroySampler(device,sampler,nullptr); }
        if (pool) { std::fprintf(stderr,"Vulkan: Texture binding destroyed.\n"); }
    }
};
VkDevice detail::TextureBindingAccess::device(const TextureBindingRef& ref) { return ref.state_ ? ref.state_->device : VK_NULL_HANDLE; }
VkDescriptorSetLayout detail::TextureBindingAccess::layout(const TextureBindingRef& ref) { return ref.state_ ? ref.state_->layout : VK_NULL_HANDLE; }
VkDescriptorSet detail::TextureBindingAccess::set(const TextureBindingRef& ref) { return ref.state_ ? ref.state_->set : VK_NULL_HANDLE; }

TextureBindingRef createTextureBinding(const VulkanDevice& device, TextureRef texture, const SamplerDesc& sampler)
{
    const auto* image = texture.state_.get();
    if (!device.handle() || !image || image->device != device.handle() || !image->sampled ||
        static_cast<unsigned>(sampler.wrapU) > static_cast<unsigned>(SamplerDesc::Wrap::Mirror) ||
        static_cast<unsigned>(sampler.wrapV) > static_cast<unsigned>(SamplerDesc::Wrap::Mirror) ||
        static_cast<unsigned>(sampler.mips) > static_cast<unsigned>(SamplerDesc::Mips::Linear)) { return {}; }
    const auto getProc = detail::loadFunction<PFN_vkGetDeviceProcAddr>(device.physicalDeviceInfo().instance,"vkGetDeviceProcAddr");
    if (!getProc) { return {}; }
    auto state = std::make_shared<detail::TextureBindingState>(); state->device = device.handle(); state->texture = std::move(texture);
    const auto load = [&](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(getProc(device.handle(),name)); return fn != nullptr; };
#define LOAD(type, name) PFN_vk##type name{}; if (!load(name,"vk" #type)) { return {}; }
    LOAD(CreateSampler,createSampler) LOAD(CreateDescriptorSetLayout,createLayout) LOAD(CreateDescriptorPool,createPool)
    LOAD(AllocateDescriptorSets,allocateSets) LOAD(UpdateDescriptorSets,updateSets)
#undef LOAD
    if (!load(state->destroyPool,"vkDestroyDescriptorPool") || !load(state->destroyLayout,"vkDestroyDescriptorSetLayout") || !load(state->destroySampler,"vkDestroySampler")) { return {}; }
    const auto wrap = [](SamplerDesc::Wrap mode) { return mode == SamplerDesc::Wrap::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : mode == SamplerDesc::Wrap::Clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; };
    VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    info.magFilter = sampler.linearMag ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; info.minFilter = sampler.linearMin ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.mipmapMode = sampler.mips == SamplerDesc::Mips::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = wrap(sampler.wrapU); info.addressModeV = wrap(sampler.wrapV); info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxLod = sampler.mips == SamplerDesc::Mips::None ? .25f : static_cast<float>(image->mipLevels - 1);
    if (!createOwned(device.handle(),createSampler,info,state->sampler,"create sampler")) { return {}; }
    const VkDescriptorSetLayoutBinding bindings[] = {{BaseColorImageBinding,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
        {BaseColorSamplerBinding,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}};
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layoutInfo.bindingCount = 2; layoutInfo.pBindings = bindings;
    if (!createOwned(device.handle(),createLayout,layoutInfo,state->layout,"texture binding layout")) { return {}; }
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_SAMPLER,1}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; poolInfo.maxSets = 1; poolInfo.poolSizeCount = 2; poolInfo.pPoolSizes = sizes;
    if (!createOwned(device.handle(),createPool,poolInfo,state->pool,"texture descriptor pool")) { return {}; }
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; allocateInfo.descriptorPool = state->pool;
    allocateInfo.descriptorSetCount = 1; allocateInfo.pSetLayouts = &state->layout;
    VkDescriptorSet set{};
    if (!detail::checkResult(allocateSets(device.handle(),&allocateInfo,&set),"texture descriptor allocation")) { return {}; }
    state->set = set;
    const VkDescriptorImageInfo images[] = {{VK_NULL_HANDLE,image->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{state->sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED}};
    VkWriteDescriptorSet writes[2]{};
    for (uint32_t index = 0; index < 2; ++index)
    {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[index].dstSet = state->set; writes[index].dstBinding = bindings[index].binding;
        writes[index].descriptorCount = 1; writes[index].descriptorType = bindings[index].descriptorType; writes[index].pImageInfo = &images[index];
    }
    updateSets(device.handle(),2,writes,0,nullptr);
    std::fprintf(stderr,"Vulkan: Texture binding created.\n");
    TextureBindingRef result; result.state_ = std::move(state); return result;
}
}
