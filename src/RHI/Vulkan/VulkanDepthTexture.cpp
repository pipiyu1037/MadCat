#include "VulkanDepthTexture.h"
#include "VulkanTextureState.h"

#include "VulkanDevice.h"
#include "VulkanFunctions.h"

#include <type_traits>
#include <utility>

namespace madcat::rhi
{
uint32_t detail::TextureAccess::mipLevels(const TextureRef& texture) noexcept { return texture.state_ ? texture.state_->mipLevels : 0; }
uint32_t TextureRef::width() const noexcept { return state_ ? state_->width : 0; }
uint32_t TextureRef::height() const noexcept { return state_ ? state_->height : 0; }
VkDevice detail::TextureAccess::device(const TextureRef& texture) noexcept { return texture.state_ ? texture.state_->device : VK_NULL_HANDLE; }
VkImage detail::TextureAccess::image(const TextureRef& texture) noexcept { return texture.state_ ? texture.state_->image : VK_NULL_HANDLE; }
VkImageView detail::TextureAccess::view(const TextureRef& texture) noexcept { return texture.state_ ? texture.state_->view : VK_NULL_HANDLE; }
VkFormat detail::TextureAccess::format(const TextureRef& texture) noexcept { return texture.state_ ? texture.state_->format : VK_FORMAT_UNDEFINED; }

TextureRef createDepthTexture(const VulkanDevice& device, uint32_t width, uint32_t height, bool sampled)
{
    if (!device.handle() || width == 0 || height == 0)
    {
        std::fprintf(stderr, "Vulkan error: depth texture requires an initialized Device and nonzero extent.\n");
        return {};
    }
    const auto instance = device.physicalDeviceInfo().instance;
    const auto getFormats = detail::loadFunction<PFN_vkGetPhysicalDeviceFormatProperties>(instance, "vkGetPhysicalDeviceFormatProperties");
    const auto getImageFormats = detail::loadFunction<PFN_vkGetPhysicalDeviceImageFormatProperties>(instance, "vkGetPhysicalDeviceImageFormatProperties");
    const auto getMemory = detail::loadFunction<PFN_vkGetPhysicalDeviceMemoryProperties>(instance, "vkGetPhysicalDeviceMemoryProperties");
    const auto getProc = detail::loadFunction<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
    if (!getFormats || !getImageFormats || !getMemory || !getProc) { return {}; }
    auto state = std::make_shared<detail::TextureState>();
    state->device = device.handle();
    state->width = width;
    state->height = height;
    state->sampled = sampled;
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | (sampled ? VK_IMAGE_USAGE_SAMPLED_BIT : 0);
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkGetImageMemoryRequirements getRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkBindImageMemory bindMemory = nullptr;
    PFN_vkCreateImageView createView = nullptr;
    const auto load = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(getProc(state->device, name));
        if (!function) { std::fprintf(stderr, "Vulkan error: required device function %s is unavailable.\n", name); }
        return function != nullptr;
    };
    if (!load(createImage, "vkCreateImage") || !load(state->destroyImage, "vkDestroyImage") ||
        !load(getRequirements, "vkGetImageMemoryRequirements") || !load(allocateMemory, "vkAllocateMemory") ||
        !load(state->freeMemory, "vkFreeMemory") || !load(bindMemory, "vkBindImageMemory") ||
        !load(createView, "vkCreateImageView") || !load(state->destroyView, "vkDestroyImageView")) { return {}; }

    for (const auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM})
    {
        VkFormatProperties properties{};
        getFormats(device.physicalDevice(), format, &properties);
        const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | (sampled ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT : 0);
        if ((properties.optimalTilingFeatures & required) != required) { continue; }
        VkImageFormatProperties imageProperties{};
        const auto result = getImageFormats(device.physicalDevice(), format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            usage, 0, &imageProperties);
        if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) { continue; }
        if (!detail::checkResult(result, "vkGetPhysicalDeviceImageFormatProperties")) { return {}; }
        if (width > imageProperties.maxExtent.width || height > imageProperties.maxExtent.height ||
            imageProperties.maxExtent.depth < 1 || imageProperties.maxMipLevels < 1 || imageProperties.maxArrayLayers < 1 ||
            (imageProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0) { continue; }
        state->format = format;
        break;
    }
    if (state->format == VK_FORMAT_UNDEFINED)
    {
        std::fprintf(stderr, "Vulkan error: no D32_SFLOAT/D16_UNORM depth attachment supports %u x %u.\n", width, height);
        return {};
    }
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = state->format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    if (!detail::checkResult(createImage(state->device, &imageInfo, nullptr, &image), "vkCreateImage(depth)")) { return {}; }
    state->image = image;
    VkMemoryRequirements requirements{};
    getRequirements(state->device, image, &requirements);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    getMemory(device.physicalDevice(), &memoryProperties);
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
    {
        if ((requirements.memoryTypeBits & (uint32_t{1} << index)) != 0 &&
            (memoryProperties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0)
        {
            memoryType = index;
            break;
        }
    }
    if (memoryType == UINT32_MAX)
    {
        std::fprintf(stderr, "Vulkan error: depth texture has no compatible DEVICE_LOCAL memory type.\n");
        return {};
    }
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.image = image;
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = &dedicated;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (!detail::checkResult(allocateMemory(state->device, &allocation, nullptr, &memory), "vkAllocateMemory(depth)")) { return {}; }
    state->memory = memory;
    if (!detail::checkResult(bindMemory(state->device, image, memory, 0), "vkBindImageMemory")) { return {}; }
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = state->format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (!detail::checkResult(createView(state->device, &viewInfo, nullptr, &view), "vkCreateImageView(depth)")) { return {}; }
    state->view = view;
    std::fprintf(stderr, "Vulkan: %s texture created; %u x %u, format %d, memory type %u.\n", sampled ? "Sampled depth" : "Depth", width, height, static_cast<int>(state->format), memoryType);
    TextureRef result;
    result.state_ = std::move(state);
    return result;
}
}
