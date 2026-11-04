#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "RHI/Vulkan/VulkanDevice.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanDepthTexture.h"
#include "RHI/Vulkan/VulkanShaderModule.h"
#include "RHI/Vulkan/VulkanTrianglePipeline.h"
#include "Shader/Spirv.h"

#include <bit>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace
{
void check(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

// A structural fixture, used only by the CPU loader and mocked Vulkan calls.
// It deliberately does not claim to be a semantically valid Shader.
const std::vector<uint32_t> fixture = {0x07230203, 0x00010600, 0, 1, 0, 0x00010000};
std::vector<uint32_t> expectedCode = fixture;
std::set<VkShaderModule> liveModules;
std::string_view missingFunction;
bool failCreation = false;
int creates = 0;
bool expectedDemote = true;
int destroys = 0;
int cases = 0;
uintptr_t serial = 100;
const auto deviceToken = reinterpret_cast<VkDevice>(1);
const auto instanceToken = reinterpret_cast<VkInstance>(2);
std::set<VkPipelineLayout> liveLayouts;
std::set<VkPipeline> livePipelines;
std::string_view failingPipelineOperation;
std::string recorded;
VkExtent2D expectedExtent{};
madcat::rhi::DrawTransform expectedTransform;
madcat::rhi::DrawTransform recordedTransform;
VkFormat expectedFormat = VK_FORMAT_B8G8R8A8_SRGB;
VkFormat expectedDepthFormat = VK_FORMAT_D32_SFLOAT;
VkShaderModule expectedVertex = VK_NULL_HANDLE;
VkShaderModule expectedFragment = VK_NULL_HANDLE;
int layoutCreates = 0, pipelineCreates = 0;
const auto commandToken = reinterpret_cast<VkCommandBuffer>(6);
std::set<VkBuffer> liveBuffers;
std::map<VkBuffer, VkBufferUsageFlags> bufferUsages;
VkBuffer expectedVertices = VK_NULL_HANDLE, expectedIndices = VK_NULL_HANDLE;
uint32_t expectedIndexCount = 6;
uint32_t expectedFirstIndex = 0;
std::map<VkDeviceMemory, std::vector<std::byte>> liveMemory;
std::map<VkBuffer, VkDeviceMemory> boundMemory;
std::set<VkDeviceMemory> mappedMemory;
std::string_view failingBufferOperation;
uint32_t memoryTypeBits = 2;
bool noCoherentMemory = false;
int bufferCreates = 0;
std::map<VkImage, VkFormat> liveImages;
std::map<VkImageView, VkImage> liveViews;
std::map<VkImage, VkDeviceMemory> boundImages;
std::string_view failingDepthOperation;
bool noD32 = false, noD16 = false, rejectD32Image = false;
VkExtent3D maxDepthExtent{4096, 4096, 1};
VkSampleCountFlags depthSamples = VK_SAMPLE_COUNT_1_BIT;
uint32_t depthMemoryTypeBits = 4;
int imageCreates = 0;
constexpr madcat::rhi::ColoredVertex geometry[] = {
    {{0, -0.6f, 0}, {1, 0, 0}}, {{0.6f, 0.6f, 0}, {0, 1, 0}}, {{-0.6f, 0.6f, 0}, {0, 0, 1}}
};
constexpr uint32_t geometryIndices[] = {2, 0, 1, 2, 1, 0};

void writeWords(const std::filesystem::path& file, const std::vector<uint32_t>& words)
{
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
    check(static_cast<bool>(output), "Could not write test fixture");
}

VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice, const VkDeviceCreateInfo* info, const VkAllocationCallbacks*, VkDevice* device)
{
    const auto* features = static_cast<const VkPhysicalDeviceVulkan13Features*>(info->pNext);
    check(features && bool(features->shaderDemoteToHelperInvocation) == expectedDemote, "Demote feature enablement differs from query support");
    *device = deviceToken;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice, const VkAllocationCallbacks*)
{
    check(liveModules.empty(), "Device destroyed before its modules");
    check(liveLayouts.empty() && livePipelines.empty(), "Device destroyed before pipeline/layout");
    check(liveBuffers.empty() && liveMemory.empty(), "Device destroyed before buffer/memory");
    check(liveImages.empty() && liveViews.empty(), "Device destroyed before image/view");
}
VKAPI_ATTR VkResult VKAPI_CALL waitIdle(VkDevice) { return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL getQueue(VkDevice, uint32_t, uint32_t, VkQueue* queue) { *queue = reinterpret_cast<VkQueue>(3); }

VKAPI_ATTR void VKAPI_CALL memoryProperties(VkPhysicalDevice physical, VkPhysicalDeviceMemoryProperties* properties)
{
    check(physical == reinterpret_cast<VkPhysicalDevice>(4), "Memory query used wrong physical device");
    *properties = {};
    properties->memoryTypeCount = 3;
    properties->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    properties->memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        (noCoherentMemory ? 0 : VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    properties->memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    properties->memoryHeapCount = 1;
    properties->memoryHeaps[0].size = 1024 * 1024;
}
VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice device, const VkBufferCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkBuffer* buffer)
{
    ++bufferCreates;
    check(device == deviceToken && !allocator && info->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && !info->pNext &&
        info->size > 0 && info->size <= 256 && info->flags == 0 &&
        (info->usage == VK_BUFFER_USAGE_VERTEX_BUFFER_BIT || info->usage == VK_BUFFER_USAGE_INDEX_BUFFER_BIT) &&
        info->sharingMode == VK_SHARING_MODE_EXCLUSIVE && info->queueFamilyIndexCount == 0, "Wrong buffer creation parameters");
    *buffer = reinterpret_cast<VkBuffer>(9999);
    if (failingBufferOperation == "create") { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *buffer = reinterpret_cast<VkBuffer>(++serial);
    liveBuffers.insert(*buffer);
    bufferUsages.emplace(*buffer, info->usage);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* allocator)
{
    check(device == deviceToken && !allocator && liveBuffers.erase(buffer) == 1, "Invalid or repeated buffer destruction");
    check(bufferUsages.erase(buffer) == 1, "Buffer usage metadata missing");
    const auto found = boundMemory.find(buffer);
    if (found != boundMemory.end())
    {
        check(liveMemory.contains(found->second) && !mappedMemory.contains(found->second), "Buffer memory freed/mapped during destruction");
        boundMemory.erase(found);
    }
}
VKAPI_ATTR void VKAPI_CALL getRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* requirements)
{
    check(device == deviceToken && liveBuffers.contains(buffer), "Requirements for invalid buffer");
    *requirements = {256, 64, memoryTypeBits};
}
VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice device, const VkMemoryAllocateInfo* info,
    const VkAllocationCallbacks* allocator, VkDeviceMemory* memory)
{
    const auto* dedicated = static_cast<const VkMemoryDedicatedAllocateInfo*>(info->pNext);
    check(device == deviceToken && !allocator && info->sType == VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO &&
        dedicated && dedicated->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO && !dedicated->pNext,
        "Wrong dedicated allocation structure");
    const bool depth = dedicated->image != VK_NULL_HANDLE;
    check(depth ? info->allocationSize == 16384 && info->memoryTypeIndex == 2 && liveImages.contains(dedicated->image) && !dedicated->buffer :
        info->allocationSize == 256 && info->memoryTypeIndex == 1 && liveBuffers.contains(dedicated->buffer), "Wrong memory allocation or type selection");
    *memory = reinterpret_cast<VkDeviceMemory>(9999);
    if (failingBufferOperation == "allocate" || failingDepthOperation == "allocate") { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *memory = reinterpret_cast<VkDeviceMemory>(++serial);
    liveMemory.emplace(*memory, std::vector<std::byte>(static_cast<size_t>(info->allocationSize)));
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* allocator)
{
    check(device == deviceToken && !allocator && !mappedMemory.contains(memory), "Invalid free or memory still mapped");
    for (const auto& [buffer, bound] : boundMemory) { check(bound != memory, "Memory freed before buffer"); (void)buffer; }
    for (const auto& [image, bound] : boundImages) { check(bound != memory, "Memory freed before image"); (void)image; }
    check(liveMemory.erase(memory) == 1, "Invalid/repeated memory free");
}
VKAPI_ATTR VkResult VKAPI_CALL bindMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset)
{
    check(device == deviceToken && liveBuffers.contains(buffer) && liveMemory.contains(memory) && offset == 0, "Wrong memory bind");
    if (failingBufferOperation == "bind") { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    check(boundMemory.emplace(buffer, memory).second, "Memory bound twice");
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL mapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
    VkDeviceSize size, VkMemoryMapFlags flags, void** mapped)
{
    check(device == deviceToken && liveMemory.contains(memory) && offset == 0 && size > 0 && size <= 256 && flags == 0,
        "Wrong memory mapping");
    *mapped = reinterpret_cast<void*>(1);
    if (failingBufferOperation == "map") { return VK_ERROR_MEMORY_MAP_FAILED; }
    check(mappedMemory.insert(memory).second, "Memory mapped twice");
    *mapped = liveMemory.at(memory).data();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL unmapMemory(VkDevice device, VkDeviceMemory memory)
{
    check(device == deviceToken && mappedMemory.erase(memory) == 1, "Invalid memory unmap");
}

VKAPI_ATTR void VKAPI_CALL formatProperties(VkPhysicalDevice physical, VkFormat format, VkFormatProperties* properties)
{
    check(physical == reinterpret_cast<VkPhysicalDevice>(4) && (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM), "Wrong depth format query");
    *properties = {};
    if (format == VK_FORMAT_D32_SFLOAT ? !noD32 : !noD16) { properties->optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT; }
}
VKAPI_ATTR VkResult VKAPI_CALL imageProperties(VkPhysicalDevice physical, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties* properties)
{
    check(physical == reinterpret_cast<VkPhysicalDevice>(4) && type == VK_IMAGE_TYPE_2D && tiling == VK_IMAGE_TILING_OPTIMAL &&
        usage == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT && flags == 0, "Wrong image capability query");
    if (failingDepthOperation == "query") { return VK_ERROR_OUT_OF_HOST_MEMORY; }
    if (rejectD32Image && format == VK_FORMAT_D32_SFLOAT) { return VK_ERROR_FORMAT_NOT_SUPPORTED; }
    *properties = {maxDepthExtent, 1, 1, depthSamples, 65536};
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL createImage(VkDevice device, const VkImageCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkImage* image)
{
    ++imageCreates;
    check(device == deviceToken && !allocator && info->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO && !info->pNext && info->flags == 0 &&
        info->imageType == VK_IMAGE_TYPE_2D && info->extent.width == 64 && info->extent.height == 48 && info->extent.depth == 1 &&
        info->mipLevels == 1 && info->arrayLayers == 1 && info->samples == VK_SAMPLE_COUNT_1_BIT && info->tiling == VK_IMAGE_TILING_OPTIMAL &&
        info->usage == VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT && info->sharingMode == VK_SHARING_MODE_EXCLUSIVE &&
        info->queueFamilyIndexCount == 0 && info->initialLayout == VK_IMAGE_LAYOUT_UNDEFINED, "Wrong depth image creation");
    check(info->format == VK_FORMAT_D32_SFLOAT || info->format == VK_FORMAT_D16_UNORM, "Wrong depth image format");
    *image = reinterpret_cast<VkImage>(9999);
    if (failingDepthOperation == "image") { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *image = reinterpret_cast<VkImage>(++serial);
    liveImages.emplace(*image, info->format);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* allocator)
{
    check(device == deviceToken && !allocator, "Wrong image destruction");
    for (const auto& [view, owner] : liveViews) { check(owner != image, "Image destroyed before view"); (void)view; }
    check(liveImages.erase(image) == 1, "Invalid/repeated image destruction");
    const auto memory = boundImages.find(image);
    if (memory != boundImages.end()) { check(liveMemory.contains(memory->second), "Image memory freed too soon"); boundImages.erase(memory); }
}
VKAPI_ATTR void VKAPI_CALL imageRequirements(VkDevice device, VkImage image, VkMemoryRequirements* requirements)
{
    check(device == deviceToken && liveImages.contains(image), "Invalid image requirements");
    *requirements = {16384, 256, depthMemoryTypeBits};
}
VKAPI_ATTR VkResult VKAPI_CALL bindImage(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset)
{
    check(device == deviceToken && liveImages.contains(image) && liveMemory.contains(memory) && offset == 0, "Wrong image memory bind");
    if (failingDepthOperation == "bind") { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    check(boundImages.emplace(image, memory).second, "Image bound twice");
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL createImageView(VkDevice device, const VkImageViewCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkImageView* view)
{
    check(device == deviceToken && !allocator && info->sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO && !info->pNext && info->flags == 0 &&
        boundImages.contains(info->image) && info->format == liveImages.at(info->image) && info->viewType == VK_IMAGE_VIEW_TYPE_2D &&
        info->subresourceRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT && info->subresourceRange.baseMipLevel == 0 &&
        info->subresourceRange.levelCount == 1 && info->subresourceRange.baseArrayLayer == 0 && info->subresourceRange.layerCount == 1, "Wrong depth view");
    *view = reinterpret_cast<VkImageView>(9999);
    if (failingDepthOperation == "view") { return VK_ERROR_OUT_OF_HOST_MEMORY; }
    *view = reinterpret_cast<VkImageView>(++serial);
    liveViews.emplace(*view, info->image);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyImageView(VkDevice device, VkImageView view, const VkAllocationCallbacks* allocator)
{
    check(device == deviceToken && !allocator && liveViews.contains(view) && liveImages.contains(liveViews.at(view)), "Invalid view destruction/order");
    liveViews.erase(view);
}

VKAPI_ATTR VkResult VKAPI_CALL createModule(VkDevice device, const VkShaderModuleCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkShaderModule* module)
{
    ++creates;
    check(device == deviceToken && allocator == nullptr && info->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO &&
        info->flags == 0 && info->pNext == nullptr, "Incorrect module creation parameters");
    check(info->codeSize == expectedCode.size() * sizeof(uint32_t) &&
        reinterpret_cast<uintptr_t>(info->pCode) % alignof(uint32_t) == 0 &&
        std::memcmp(info->pCode, expectedCode.data(), info->codeSize) == 0, "Incorrect or misaligned module bytecode");
    // An unsuccessful output is not an owned handle and must not be destroyed.
    *module = reinterpret_cast<VkShaderModule>(9999);
    if (failCreation) { return VK_ERROR_OUT_OF_HOST_MEMORY; }
    *module = reinterpret_cast<VkShaderModule>(++serial);
    liveModules.insert(*module);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyModule(VkDevice device, VkShaderModule module, const VkAllocationCallbacks* allocator)
{
    check(device == deviceToken && allocator == nullptr && liveModules.erase(module) == 1,
        "Invalid, repeated or wrong-Device module destruction");
    ++destroys;
}

VKAPI_ATTR VkResult VKAPI_CALL createLayout(VkDevice device, const VkPipelineLayoutCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkPipelineLayout* layout)
{
    ++layoutCreates;
    check(device == deviceToken && allocator == nullptr && info->sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO &&
        info->pNext == nullptr && info->flags == 0 && info->setLayoutCount == 0 && info->pushConstantRangeCount == 1 &&
        info->pPushConstantRanges[0].offset == 0 && info->pPushConstantRanges[0].size == 128 &&
        info->pPushConstantRanges[0].stageFlags == VK_SHADER_STAGE_VERTEX_BIT, "Incorrect matrix Push Constant layout");
    *layout = reinterpret_cast<VkPipelineLayout>(9999);
    if (failingPipelineOperation == "layout") { return VK_ERROR_OUT_OF_HOST_MEMORY; }
    *layout = reinterpret_cast<VkPipelineLayout>(++serial);
    liveLayouts.insert(*layout);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyLayout(VkDevice device, VkPipelineLayout layout, const VkAllocationCallbacks* allocator)
{
    check(device == deviceToken && allocator == nullptr && livePipelines.empty() && liveLayouts.erase(layout) == 1, "Pipeline layout destruction invalid or out of order");
}
VKAPI_ATTR VkResult VKAPI_CALL createPipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkGraphicsPipelineCreateInfo* info, const VkAllocationCallbacks* allocator, VkPipeline* pipeline)
{
    ++pipelineCreates;
    check(device == deviceToken && cache == VK_NULL_HANDLE && count == 1 && allocator == nullptr &&
        info->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO && info->renderPass == VK_NULL_HANDLE &&
        info->flags == 0 && info->stageCount == 2 && liveLayouts.contains(info->layout), "Incorrect pipeline creation");
    const auto& vertex = info->pStages[0];
    const auto& fragment = info->pStages[1];
    check(vertex.stage == VK_SHADER_STAGE_VERTEX_BIT && vertex.module == expectedVertex && std::strcmp(vertex.pName, "vertexMain") == 0 &&
        fragment.stage == VK_SHADER_STAGE_FRAGMENT_BIT && fragment.module == expectedFragment && std::strcmp(fragment.pName, "fragmentMain") == 0, "Wrong entry, stage or module");
    const auto* rendering = static_cast<const VkPipelineRenderingCreateInfo*>(info->pNext);
    check(rendering && rendering->sType == VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO && rendering->pNext == nullptr &&
        rendering->viewMask == 0 && rendering->colorAttachmentCount == 1 && *rendering->pColorAttachmentFormats == expectedFormat &&
        rendering->depthAttachmentFormat == expectedDepthFormat && rendering->stencilAttachmentFormat == VK_FORMAT_UNDEFINED, "Wrong dynamic-rendering formats");
    const auto& input = *info->pVertexInputState;
    check(input.vertexBindingDescriptionCount == 1 && input.vertexAttributeDescriptionCount == 4 &&
        input.pVertexBindingDescriptions[0].binding == 0 && input.pVertexBindingDescriptions[0].stride == 44 &&
        input.pVertexBindingDescriptions[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX &&
        input.pVertexAttributeDescriptions[0].location == 0 && input.pVertexAttributeDescriptions[0].binding == 0 &&
        input.pVertexAttributeDescriptions[0].format == VK_FORMAT_R32G32B32_SFLOAT && input.pVertexAttributeDescriptions[0].offset == 0 &&
        input.pVertexAttributeDescriptions[1].location == 1 && input.pVertexAttributeDescriptions[1].binding == 0 &&
        input.pVertexAttributeDescriptions[1].format == VK_FORMAT_R32G32B32_SFLOAT && input.pVertexAttributeDescriptions[1].offset == 12 &&
        input.pVertexAttributeDescriptions[2].location == 2 && input.pVertexAttributeDescriptions[2].binding == 0 &&
        input.pVertexAttributeDescriptions[2].format == VK_FORMAT_R32G32B32_SFLOAT && input.pVertexAttributeDescriptions[2].offset == 24 &&
        input.pVertexAttributeDescriptions[3].location == 3 && input.pVertexAttributeDescriptions[3].binding == 0 &&
        input.pVertexAttributeDescriptions[3].format == VK_FORMAT_R32G32_SFLOAT && input.pVertexAttributeDescriptions[3].offset == 36,
        "Wrong vertex buffer layout");
    check(info->pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && info->pInputAssemblyState->primitiveRestartEnable == VK_FALSE &&
        info->pRasterizationState->polygonMode == VK_POLYGON_MODE_FILL && info->pRasterizationState->cullMode == VK_CULL_MODE_NONE &&
        info->pRasterizationState->lineWidth == 1.0f && info->pRasterizationState->rasterizerDiscardEnable == VK_FALSE &&
        info->pMultisampleState->rasterizationSamples == VK_SAMPLE_COUNT_1_BIT && info->pDepthStencilState->depthTestEnable == VK_TRUE &&
        info->pDepthStencilState->depthWriteEnable == VK_TRUE && info->pDepthStencilState->depthCompareOp == VK_COMPARE_OP_LESS &&
        info->pDepthStencilState->stencilTestEnable == VK_FALSE, "Unexpected triangle fixed-function state");
    check(info->pColorBlendState->attachmentCount == 1 && info->pColorBlendState->pAttachments[0].blendEnable == VK_FALSE &&
        info->pColorBlendState->pAttachments[0].colorWriteMask == 15 && info->pDynamicState->dynamicStateCount == 2 &&
        info->pDynamicState->pDynamicStates[0] == VK_DYNAMIC_STATE_VIEWPORT && info->pDynamicState->pDynamicStates[1] == VK_DYNAMIC_STATE_SCISSOR &&
        info->pViewportState->viewportCount == 1 && info->pViewportState->scissorCount == 1, "Wrong blend/viewport/scissor state");
    *pipeline = VK_NULL_HANDLE;
    if (failingPipelineOperation == "pipeline") { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *pipeline = reinterpret_cast<VkPipeline>(++serial);
    livePipelines.insert(*pipeline);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* allocator)
{
    check(device == deviceToken && allocator == nullptr && livePipelines.erase(pipeline) == 1, "Invalid/repeated pipeline destruction");
}
VKAPI_ATTR void VKAPI_CALL bindPipeline(VkCommandBuffer command, VkPipelineBindPoint point, VkPipeline pipeline)
{
    check(command == commandToken && point == VK_PIPELINE_BIND_POINT_GRAPHICS && livePipelines.contains(pipeline) && recorded.empty(), "Invalid pipeline bind/order");
    recorded += 'B';
}
VKAPI_ATTR void VKAPI_CALL setViewport(VkCommandBuffer command, uint32_t first, uint32_t count, const VkViewport* viewport)
{
    check(command == commandToken && recorded == "B" && first == 0 && count == 1 && viewport->x == 0 && viewport->y == 0 &&
        viewport->width == static_cast<float>(expectedExtent.width) && viewport->height == static_cast<float>(expectedExtent.height) &&
        viewport->minDepth == 0 && viewport->maxDepth == 1, "Wrong dynamic viewport/order");
    recorded += 'V';
}
VKAPI_ATTR void VKAPI_CALL setScissor(VkCommandBuffer command, uint32_t first, uint32_t count, const VkRect2D* scissor)
{
    check(command == commandToken && recorded == "BV" && first == 0 && count == 1 && scissor->offset.x == 0 && scissor->offset.y == 0 &&
        scissor->extent.width == expectedExtent.width && scissor->extent.height == expectedExtent.height, "Wrong dynamic scissor/order");
    recorded += 'S';
}
VKAPI_ATTR void VKAPI_CALL drawIndexed(VkCommandBuffer command, uint32_t indices, uint32_t instances,
    uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    check(command == commandToken && recorded == "BVSGIP" && indices == expectedIndexCount && instances == 1 &&
        firstIndex == expectedFirstIndex && vertexOffset == 0 && firstInstance == 0, "Wrong DrawIndexed arguments/order");
    recorded += 'D';
}
VKAPI_ATTR void VKAPI_CALL bindVertices(VkCommandBuffer command, uint32_t first, uint32_t count,
    const VkBuffer* buffers, const VkDeviceSize* offsets)
{
    check(command == commandToken && recorded == "BVS" && first == 0 && count == 1 && offsets[0] == 0 &&
        buffers[0] == expectedVertices && bufferUsages.at(buffers[0]) == VK_BUFFER_USAGE_VERTEX_BUFFER_BIT &&
        liveBuffers.contains(buffers[0]) && boundMemory.contains(buffers[0]), "Wrong vertex buffer binding/order");
    recorded += 'G';
}
VKAPI_ATTR void VKAPI_CALL bindIndices(VkCommandBuffer command, VkBuffer buffer, VkDeviceSize offset, VkIndexType type)
{
    check(command == commandToken && recorded == "BVSG" && buffer == expectedIndices &&
        bufferUsages.at(buffer) == VK_BUFFER_USAGE_INDEX_BUFFER_BIT && boundMemory.contains(buffer) &&
        offset == 0 && type == VK_INDEX_TYPE_UINT32, "Wrong index buffer, offset, type or binding order");
    recorded += 'I';
}
VKAPI_ATTR void VKAPI_CALL pushConstants(VkCommandBuffer command, VkPipelineLayout layout,
    VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void* data)
{
    check(command == commandToken && recorded == "BVSGI" && liveLayouts.contains(layout) &&
        stages == VK_SHADER_STAGE_VERTEX_BIT && offset == 0 && size == 128 &&
        std::memcmp(data, &expectedTransform, size) == 0, "Wrong matrix bytes, range, stage or recording order");
    std::memcpy(&recordedTransform, data, size);
    recorded += 'P';
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice device, const char* name)
{
    check(device == deviceToken, "Function lookup used wrong Device");
    if (name == missingFunction) { return nullptr; }
    if (std::strcmp(name, "vkCreateImage") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(createImage); }
    if (std::strcmp(name, "vkDestroyImage") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(destroyImage); }
    if (std::strcmp(name, "vkGetImageMemoryRequirements") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(imageRequirements); }
    if (std::strcmp(name, "vkBindImageMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(bindImage); }
    if (std::strcmp(name, "vkCreateImageView") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(createImageView); }
    if (std::strcmp(name, "vkDestroyImageView") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(destroyImageView); }
    if (std::strcmp(name, "vkCmdPushConstants") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(pushConstants); }
    if (std::strcmp(name, "vkCreateBuffer") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(createBuffer); }
    if (std::strcmp(name, "vkDestroyBuffer") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(destroyBuffer); }
    if (std::strcmp(name, "vkGetBufferMemoryRequirements") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(getRequirements); }
    if (std::strcmp(name, "vkAllocateMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(allocateMemory); }
    if (std::strcmp(name, "vkFreeMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(freeMemory); }
    if (std::strcmp(name, "vkBindBufferMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(bindMemory); }
    if (std::strcmp(name, "vkMapMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(mapMemory); }
    if (std::strcmp(name, "vkUnmapMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(unmapMemory); }
    if (std::strcmp(name, "vkCmdBindVertexBuffers") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(bindVertices); }
    if (std::strcmp(name, "vkCmdBindIndexBuffer") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(bindIndices); }
    if (std::strcmp(name, "vkCreateShaderModule") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(createModule); }
    if (std::strcmp(name, "vkDestroyShaderModule") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(destroyModule); }
    if (std::strcmp(name, "vkDeviceWaitIdle") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(waitIdle); }
    if (std::strcmp(name, "vkGetDeviceQueue") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(getQueue); }
    if (std::strcmp(name, "vkCreatePipelineLayout") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(createLayout); }
    if (std::strcmp(name, "vkDestroyPipelineLayout") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(destroyLayout); }
    if (std::strcmp(name, "vkCreateGraphicsPipelines") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(createPipelines); }
    if (std::strcmp(name, "vkDestroyPipeline") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(destroyPipeline); }
    if (std::strcmp(name, "vkCmdBindPipeline") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(bindPipeline); }
    if (std::strcmp(name, "vkCmdSetViewport") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(setViewport); }
    if (std::strcmp(name, "vkCmdSetScissor") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(setScissor); }
    if (std::strcmp(name, "vkCmdDrawIndexed") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(drawIndexed); }
    throw std::runtime_error("Unexpected device function lookup");
}

madcat::rhi::PhysicalDeviceCandidate candidate()
{
    madcat::rhi::PhysicalDeviceCandidate result;
    auto& device = result.device;
    device.instance = instanceToken;
    device.handle = reinterpret_cast<VkPhysicalDevice>(4);
    device.properties.apiVersion = VK_API_VERSION_1_3;
    device.properties.limits.maxDrawIndexedIndexValue = UINT32_MAX;
    device.features.dynamicRendering = device.features.synchronization2 = {true, true};
    device.features.shaderDemoteToHelperInvocation = {true, true};
    device.queueFamilies = {{VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT, 1}};
    VkExtensionProperties extension{};
    std::snprintf(extension.extensionName, sizeof(extension.extensionName), "%s", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    device.extensions.push_back(extension);
    auto& surface = result.surfaceSupport;
    surface.physicalDevice = device.handle;
    surface.surface = reinterpret_cast<VkSurfaceKHR>(5);
    surface.presentQueues = {VK_TRUE};
    surface.capabilities.emplace();
    surface.capabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    surface.formats = {{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    surface.presentModes = {VK_PRESENT_MODE_FIFO_KHR};
    return result;
}

void loaderChecks(const std::filesystem::path& root)
{
    const auto file = root / "fixture.spv";
    const auto reject = [&](const char* label) {
        std::vector<uint32_t> words = {123, 456};
        check(!madcat::shader::loadSpirv(file, words), label);
        check(words == std::vector<uint32_t>({123, 456}), "Failed read changed caller's words");
        ++cases;
    };
    writeWords(file, {});
    reject("Empty file accepted");
    for (size_t count = 1; count <= 5; ++count)
    {
        writeWords(file, {fixture.begin(), fixture.begin() + count});
        reject("Incomplete header/instruction stream accepted");
    }
    writeWords(file, fixture);
    { std::ofstream output(file, std::ios::binary | std::ios::app); output.put('x'); }
    reject("Non-word-aligned size accepted");
    for (const auto [index, value] : {std::pair<size_t, uint32_t>{0, 0}, {1, 0x00010700},
        {1, 0x00000600}, {1, 0x01010600}, {1, 0x00010601}, {3, 0}, {4, 1}, {5, 0}, {5, 0x00020000}})
    {
        auto words = fixture;
        words[index] = value;
        writeWords(file, words);
        reject("Malformed binary accepted");
    }
    for (uint32_t version : {0x00010000u, 0x00010600u})
    {
        auto words = fixture;
        words[1] = version;
        writeWords(file, words);
        std::vector<uint32_t> loaded;
        check(madcat::shader::loadSpirv(file, loaded) && loaded == words, "Native-endian round trip failed");
        for (auto& word : words) { word = std::byteswap(word); }
        writeWords(file, words);
        std::vector<uint32_t> swapped;
        check(madcat::shader::loadSpirv(file, swapped) && swapped == loaded, "Byte-swapped round trip failed");
        cases += 2;
    }
    std::vector<uint32_t> preserved = {123};
    check(!madcat::shader::loadSpirv(root / "missing.spv", preserved) && preserved == std::vector<uint32_t>({123}), "Missing path accepted");
    check(!madcat::shader::loadSpirv(root, preserved) && preserved == std::vector<uint32_t>({123}), "Directory accepted");
    cases += 2;
    const auto unicodePath = root / std::filesystem::path(u8"带空格的 Shader.spv");
    writeWords(unicodePath, fixture);
    check(madcat::shader::loadSpirv(unicodePath, preserved) && preserved == fixture, "Unicode path failed");
    ++cases;
}

void moduleChecks(const std::filesystem::path& root)
{
    using madcat::rhi::VulkanShaderModule;
    static_assert(!std::is_copy_constructible_v<VulkanShaderModule> && !std::is_move_constructible_v<VulkanShaderModule>);
    const auto file = root / "module.spv";
    writeWords(file, fixture);
    madcat::rhi::VulkanDevice device;
    {
        VulkanShaderModule empty;
        check(empty.handle() == VK_NULL_HANDLE && !empty.initialize(device, file), "Uninitialized Device accepted");
    }
    ++cases;
    const auto input = candidate();
    check(device.initialize({&input, 1}), "Could not initialize mock Device");
    for (const auto* missing : {"vkGetDeviceProcAddr", "vkCreateShaderModule", "vkDestroyShaderModule"})
    {
        missingFunction = missing;
        VulkanShaderModule module;
        check(!module.initialize(device, file) && module.handle() == VK_NULL_HANDLE && creates == 0, "Missing function acquired a module");
        ++cases;
    }
    missingFunction = {};
    {
        VulkanShaderModule module;
        failCreation = true;
        check(!module.initialize(device, file) && module.handle() == VK_NULL_HANDLE && destroys == 0, "Failed create acquired a module");
        failCreation = false;
        check(module.initialize(device, file) && module.handle() != VK_NULL_HANDLE, "Retry after failure failed");
        const auto handle = module.handle();
        const auto before = creates;
        check(!module.initialize(device, root / "missing.spv") && module.handle() == handle && creates == before, "Repeated initialization replaced module");
        cases += 3;
    }
    check(destroys == 1 && liveModules.empty(), "Successful module not destroyed exactly once");
    {
        VulkanShaderModule first;
        VulkanShaderModule second;
        check(first.initialize(device, file), "First module failed");
        failCreation = true;
        check(!second.initialize(device, file) && second.handle() == VK_NULL_HANDLE && liveModules.size() == 1, "Second-stage failure changed first module");
        failCreation = false;
    }
    check(destroys == 2 && liveModules.empty(), "Partial pair leaked a module");
    ++cases;
    {
        VulkanShaderModule module;
        const auto before = creates;
        check(!module.initialize(device, root / "missing.spv") && creates == before, "Missing file reached Vulkan");
        writeWords(file, {});
        check(!module.initialize(device, file) && creates == before, "Empty file reached Vulkan");
        writeWords(file, {0, 0, 0, 0, 0, 0});
        check(!module.initialize(device, file) && creates == before, "Corrupt file reached Vulkan");
        writeWords(file, fixture);
        check(module.initialize(device, file), "Retry after file error failed");
        cases += 4;
    }
    check(destroys == 3 && liveModules.empty(), "Retried module leaked");
}

void bufferChecks()
{
    using madcat::rhi::BufferRef;
    using madcat::rhi::createVertexBuffer;
    const auto bytes = std::as_bytes(std::span(geometry));
    BufferRef empty;
    check(!empty && empty.sizeBytes() == 0 && !madcat::rhi::detail::BufferAccess::handle(empty), "Empty reference has data");
    madcat::rhi::VulkanDevice device;
    check(!createVertexBuffer(device, bytes), "Uninitialized Device accepted");
    ++cases;
    const auto input = candidate();
    check(device.initialize({&input, 1}), "Mock Device failed");
    check(!createVertexBuffer(device, {}) && bufferCreates == 0, "Empty upload reached Vulkan");
    ++cases;
    for (const auto* name : {"vkGetPhysicalDeviceMemoryProperties", "vkGetDeviceProcAddr", "vkCreateBuffer", "vkDestroyBuffer",
        "vkGetBufferMemoryRequirements", "vkAllocateMemory", "vkFreeMemory", "vkBindBufferMemory", "vkMapMemory", "vkUnmapMemory"})
    {
        missingFunction = name;
        const auto before = bufferCreates;
        check(!createVertexBuffer(device, bytes) && bufferCreates == before && liveBuffers.empty() && liveMemory.empty(),
            "Missing function acquired resources");
        ++cases;
    }
    missingFunction = {};
    memoryTypeBits = 4;
    check(!createVertexBuffer(device, bytes) && liveBuffers.empty() && liveMemory.empty(), "Ignored memoryTypeBits");
    ++cases;
    memoryTypeBits = 2;
    noCoherentMemory = true;
    check(!createVertexBuffer(device, bytes) && liveBuffers.empty() && liveMemory.empty(), "Noncoherent memory accepted");
    ++cases;
    noCoherentMemory = false;
    for (const auto* operation : {"create", "allocate", "bind", "map"})
    {
        failingBufferOperation = operation;
        check(!createVertexBuffer(device, bytes) && liveBuffers.empty() && liveMemory.empty() && mappedMemory.empty(),
            "Failure leaked buffer/memory or mapped an unsuccessful output");
        failingBufferOperation = {};
        auto retry = createVertexBuffer(device, bytes);
        check(static_cast<bool>(retry), "Upload retry failed");
        ++cases;
    }
    check(liveBuffers.empty() && liveMemory.empty(), "Retry resource leak");
    auto source = std::to_array(geometry);
    auto buffer = createVertexBuffer(device, std::as_bytes(std::span(source)));
    check(buffer && buffer.sizeBytes() == sizeof(geometry) && mappedMemory.empty(), "Upload failed or remained mapped");
    const auto native = madcat::rhi::detail::BufferAccess::handle(buffer);
    const auto memory = boundMemory.at(native);
    source[0].position[0] = 123;
    check(std::memcmp(liveMemory.at(memory).data(), geometry, sizeof(geometry)) == 0, "Data not copied or source retained");
    auto copy = buffer;
    auto moved = std::move(buffer);
    check(!buffer && buffer.sizeBytes() == 0 && liveBuffers.size() == 1, "Move changed shared allocation");
    moved = {};
    check(copy && liveBuffers.contains(native), "First release destroyed shared buffer");
    copy = {};
    check(liveBuffers.empty() && liveMemory.empty(), "Last reference did not release buffer/memory");
    ++cases;
}

void indexBufferChecks()
{
    using madcat::rhi::createIndexBuffer;
    using madcat::rhi::detail::BufferAccess;
    madcat::rhi::VulkanDevice device;
    const auto input = candidate();
    check(!createIndexBuffer(device, geometryIndices), "Index upload accepted an uninitialized Device");
    ++cases;
    check(device.initialize({&input, 1}), "Index mock Device failed");
    const auto beforeEmpty = bufferCreates;
    check(!createIndexBuffer(device, {}) && bufferCreates == beforeEmpty, "Empty indices reached Vulkan");
    ++cases;
    const uint32_t excessive[] = {0, 1, 0x01000000};
    check(!createIndexBuffer(device, excessive) && bufferCreates == beforeEmpty, "Disabled fullDrawIndexUint32 range accepted");
    ++cases;
    {
        auto limitedInput = candidate();
        limitedInput.device.properties.limits.maxDrawIndexedIndexValue = 1;
        madcat::rhi::VulkanDevice limited;
        check(limited.initialize({&limitedInput, 1}) && !createIndexBuffer(limited, geometryIndices) && bufferCreates == beforeEmpty,
            "Queried draw index limit ignored");
        ++cases;
    }
    for (const auto* name : {"vkGetPhysicalDeviceMemoryProperties", "vkGetDeviceProcAddr", "vkCreateBuffer", "vkDestroyBuffer",
        "vkGetBufferMemoryRequirements", "vkAllocateMemory", "vkFreeMemory", "vkBindBufferMemory", "vkMapMemory", "vkUnmapMemory"})
    {
        missingFunction = name;
        const auto before = bufferCreates;
        check(!createIndexBuffer(device, geometryIndices) && bufferCreates == before && liveBuffers.empty() && liveMemory.empty(),
            "Missing index upload function acquired resources");
        ++cases;
    }
    missingFunction = {};
    memoryTypeBits = 4;
    check(!createIndexBuffer(device, geometryIndices) && liveBuffers.empty() && liveMemory.empty(), "Index upload ignored memoryTypeBits");
    ++cases;
    memoryTypeBits = 2;
    noCoherentMemory = true;
    check(!createIndexBuffer(device, geometryIndices) && liveBuffers.empty() && liveMemory.empty(), "Index upload accepted noncoherent memory");
    ++cases;
    noCoherentMemory = false;
    for (const auto* operation : {"create", "allocate", "bind", "map"})
    {
        failingBufferOperation = operation;
        check(!createIndexBuffer(device, geometryIndices) && liveBuffers.empty() && liveMemory.empty() && mappedMemory.empty(),
            "Index upload failure leaked resources");
        failingBufferOperation = {};
        auto retry = createIndexBuffer(device, geometryIndices);
        check(static_cast<bool>(retry), "Index retry failed");
        ++cases;
    }
    auto source = std::to_array(geometryIndices);
    auto uploaded = createIndexBuffer(device, source);
    check(uploaded && uploaded.sizeBytes() == sizeof(geometryIndices) && BufferAccess::maximumIndex(uploaded) == 2 &&
        BufferAccess::usage(uploaded) == VK_BUFFER_USAGE_INDEX_BUFFER_BIT && mappedMemory.empty(), "Index metadata invalid or still mapped");
    const auto native = BufferAccess::handle(uploaded);
    source.fill(0);
    check(std::memcmp(liveMemory.at(boundMemory.at(native)).data(), geometryIndices, sizeof(geometryIndices)) == 0,
        "Index upload did not copy the source");
    auto frame = uploaded;
    auto moved = std::move(uploaded);
    check(!uploaded && frame && liveBuffers.contains(native), "Index move invalidated shared data");
    moved = {};
    check(liveBuffers.contains(native), "Index released before frame reference");
    frame = {};
    check(liveBuffers.empty() && liveMemory.empty() && bufferUsages.empty(), "Index last release leaked data");
    ++cases;
}

void depthChecks()
{
    using madcat::rhi::TextureRef;
    using madcat::rhi::createDepthTexture;
    using madcat::rhi::detail::TextureAccess;
    const auto clean = [] { return liveImages.empty() && liveViews.empty() && boundImages.empty() && liveMemory.empty(); };
    TextureRef empty;
    check(!empty && empty.width() == 0 && empty.height() == 0 && !TextureAccess::image(empty) &&
        !TextureAccess::view(empty) && TextureAccess::format(empty) == VK_FORMAT_UNDEFINED, "Empty texture has data");
    madcat::rhi::VulkanDevice device;
    check(!createDepthTexture(device, 64, 48), "Uninitialized Device accepted");
    ++cases;
    const auto input = candidate();
    check(device.initialize({&input, 1}), "Mock setup failed");
    check(!createDepthTexture(device, 0, 48) && !createDepthTexture(device, 64, 0) && imageCreates == 0, "Zero extent reached Vulkan");
    ++cases;
    for (const auto* name : {"vkGetPhysicalDeviceFormatProperties", "vkGetPhysicalDeviceImageFormatProperties", "vkGetPhysicalDeviceMemoryProperties",
        "vkGetDeviceProcAddr", "vkCreateImage", "vkDestroyImage", "vkGetImageMemoryRequirements", "vkAllocateMemory", "vkFreeMemory",
        "vkBindImageMemory", "vkCreateImageView", "vkDestroyImageView"})
    {
        missingFunction = name;
        const auto before = imageCreates;
        check(!createDepthTexture(device, 64, 48) && clean() && imageCreates == before, "Missing function acquired depth objects");
        ++cases;
    }
    missingFunction = {};
    for (const auto rejectByFeature : {true, false})
    {
        noD32 = rejectByFeature;
        rejectD32Image = !rejectByFeature;
        auto fallback = createDepthTexture(device, 64, 48);
        check(fallback && TextureAccess::format(fallback) == VK_FORMAT_D16_UNORM, "D16 fallback failed");
        ++cases;
    }
    noD32 = rejectD32Image = false;
    noD32 = noD16 = true;
    check(!createDepthTexture(device, 64, 48) && clean(), "Unsupported formats accepted");
    ++cases;
    noD32 = noD16 = false;
    check(!createDepthTexture(device, 4097, 48) && clean(), "Extent limits ignored");
    ++cases;
    depthSamples = VK_SAMPLE_COUNT_4_BIT;
    check(!createDepthTexture(device, 64, 48) && clean(), "Unsupported sample count accepted");
    ++cases;
    depthSamples = VK_SAMPLE_COUNT_1_BIT;
    depthMemoryTypeBits = 2;
    check(!createDepthTexture(device, 64, 48) && clean(), "Depth memoryTypeBits ignored");
    ++cases;
    depthMemoryTypeBits = 4;
    for (const auto* operation : {"query", "image", "allocate", "bind", "view"})
    {
        failingDepthOperation = operation;
        check(!createDepthTexture(device, 64, 48) && clean(), "Depth failure leaked partial resources");
        failingDepthOperation = {};
        auto retry = createDepthTexture(device, 64, 48);
        check(retry && retry.width() == 64 && retry.height() == 48 && TextureAccess::format(retry) == VK_FORMAT_D32_SFLOAT,
            "Depth creation retry failed");
        ++cases;
    }
    check(clean(), "Retry resource leak");
    auto owner = createDepthTexture(device, 64, 48);
    const auto image = TextureAccess::image(owner);
    auto copy = owner;
    auto moved = std::move(owner);
    check(!owner && owner.width() == 0 && liveImages.size() == 1, "Texture move copied allocation");
    moved = {};
    check(copy && liveImages.contains(image) && liveViews.size() == 1, "Texture reference released too soon");
    copy = {};
    check(clean(), "Last reference did not release view/image/memory");
    ++cases;
}

void triangleChecks(const std::filesystem::path& root)
{
    using madcat::rhi::VulkanTrianglePipeline;
    for (int mode = 0; mode < 3; ++mode)
    {
        auto input = candidate();
        input.device.features.shaderDemoteToHelperInvocation = {mode != 0, mode == 2};
        expectedDemote = mode == 2;
        madcat::rhi::VulkanDevice featureDevice;
        check(featureDevice.initialize({&input,1}), "Optional demote feature selection failed");
        ++cases;
    }
    expectedDemote = true;
    static_assert(!std::is_copy_constructible_v<VulkanTrianglePipeline> && !std::is_move_constructible_v<VulkanTrianglePipeline>);
    const auto file = root / "module.spv";
    writeWords(file, fixture);
    madcat::rhi::VulkanDevice device;
    madcat::rhi::VulkanShaderModule vertex, fragment, empty;
    madcat::rhi::BufferRef vertices, indices;
    {
        VulkanTrianglePipeline pipeline;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment, vertices, indices, expectedDepthFormat), "Uninitialized Device accepted");
        check(!pipeline.recordDraw(commandToken, {640, 480}, expectedTransform), "Uninitialized pipeline recorded Draw");
        cases += 2;
    }
    const auto input = candidate();
    check(device.initialize({&input, 1}) && vertex.initialize(device, file) && fragment.initialize(device, file), "Mock setup failed");
    vertices = madcat::rhi::createVertexBuffer(device, std::as_bytes(std::span(geometry)));
    indices = madcat::rhi::createIndexBuffer(device, geometryIndices);
    check(vertices && indices, "Mock upload failed");
    expectedVertices = madcat::rhi::detail::BufferAccess::handle(vertices);
    expectedIndices = madcat::rhi::detail::BufferAccess::handle(indices);
    expectedVertex = vertex.handle();
    expectedFragment = fragment.handle();
    for (int invalid = 0; invalid < 3; ++invalid)
    {
        VulkanTrianglePipeline pipeline;
        check(!pipeline.initialize(device, invalid == 2 ? VK_FORMAT_UNDEFINED : expectedFormat,
            invalid == 0 ? empty : vertex, invalid == 1 ? empty : fragment, vertices, indices, expectedDepthFormat), "Invalid module/format accepted");
        check(layoutCreates == 0, "Invalid input reached Vulkan");
        ++cases;
    }
    for (const auto* name : {"vkGetDeviceProcAddr", "vkCreatePipelineLayout", "vkDestroyPipelineLayout", "vkCreateGraphicsPipelines",
        "vkDestroyPipeline", "vkCmdBindPipeline", "vkCmdSetViewport", "vkCmdSetScissor", "vkCmdDrawIndexed", "vkCmdBindVertexBuffers", "vkCmdBindIndexBuffer", "vkCmdPushConstants"})
    {
        missingFunction = name;
        VulkanTrianglePipeline pipeline;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment, vertices, indices, expectedDepthFormat) && pipeline.handle() == VK_NULL_HANDLE &&
            pipeline.deviceHandle() == VK_NULL_HANDLE && pipeline.colorFormat() == VK_FORMAT_UNDEFINED && layoutCreates == 0, "Missing function acquired pipeline resources");
        ++cases;
    }
    missingFunction = {};
    for (const auto* operation : {"layout", "pipeline"})
    {
        {
            VulkanTrianglePipeline pipeline;
            failingPipelineOperation = operation;
            check(!pipeline.initialize(device, expectedFormat, vertex, fragment, vertices, indices, expectedDepthFormat) && pipeline.handle() == VK_NULL_HANDLE &&
                liveLayouts.empty() && livePipelines.empty(), "Failed creation leaked a pipeline/layout");
            ++cases;
            failingPipelineOperation = {};
            check(pipeline.initialize(device, expectedFormat, vertex, fragment, vertices, indices, expectedDepthFormat), "Pipeline creation retry failed");
            const auto handle = pipeline.handle();
            const auto before = pipelineCreates;
            check(pipeline.deviceHandle() == device.handle() && pipeline.colorFormat() == expectedFormat && pipeline.depthFormat() == expectedDepthFormat &&
                !pipeline.initialize(device, expectedFormat, vertex, fragment, vertices, indices, expectedDepthFormat) && pipeline.handle() == handle && pipelineCreates == before, "Repeated creation changed live pipeline");
            ++cases;
            for (VkExtent2D extent : {VkExtent2D{640, 480}, VkExtent2D{937, 611}})
            {
                recorded.clear();
                expectedExtent = extent;
                for (size_t index = 0; index < expectedTransform.clipFromLocal.size(); ++index)
                {
                    expectedTransform.clipFromLocal[index] = static_cast<float>(index) + static_cast<float>(extent.width) / 1000.0f;
                }
                expectedTransform.normalFromLocal[1] = .25f;
                expectedTransform.view = extent.width == 640 ? madcat::rhi::GeometryView::WorldNormal : madcat::rhi::GeometryView::Texcoord;
                expectedTransform.attributes = 7;
                auto inputTransform = expectedTransform;
                check(pipeline.recordDraw(commandToken, extent, inputTransform) && recorded == "BVSGIPD", "Wrong recording sequence");
                inputTransform.clipFromLocal.fill(-1);
                check(recordedTransform.clipFromLocal == expectedTransform.clipFromLocal, "Recorded constants depend on mutable CPU data");
                ++cases;
            }
            for (int mode = 0; mode < 3; ++mode)
            {
                auto invalid = expectedTransform;
                if (mode == 0) { invalid.normalFromLocal[4] = std::numeric_limits<float>::quiet_NaN(); }
                if (mode == 1) { invalid.view = static_cast<madcat::rhi::GeometryView>(4); }
                if (mode == 2) { invalid.attributes = 128; }
                recorded.clear();
                check(!pipeline.recordDraw(commandToken, expectedExtent, invalid) && recorded.empty(), "Invalid normal/view/flags reached Vulkan");
                ++cases;
            }
            recorded.clear();
            expectedIndexCount = 3;
            expectedFirstIndex = 3;
            check(pipeline.recordIndexedDraw(commandToken, expectedExtent, {expectedTransform, 3, 3}) && recorded == "BVSGIPD",
                "Primitive subrange did not reach DrawIndexed");
            ++cases;
            expectedIndexCount = 6;
            expectedFirstIndex = 0;
            recorded.clear();
            for (const auto range : {std::pair{0u, 0u}, {0u, 4u}, {6u, 3u}, {UINT32_MAX, 3u}, {3u, UINT32_MAX}})
            {
                check(!pipeline.recordIndexedDraw(commandToken, expectedExtent, {expectedTransform, range.first, range.second}) &&
                    recorded.empty(), "Invalid Primitive range reached Vulkan");
                ++cases;
            }
            check(!pipeline.recordDraw(VK_NULL_HANDLE, {640, 480}, expectedTransform) &&
                !pipeline.recordDraw(commandToken, {0, 480}, expectedTransform) &&
                !pipeline.recordDraw(commandToken, {640, 0}, expectedTransform) && recorded.empty(), "Invalid recording input reached Vulkan");
            ++cases;
            for (const auto value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
            {
                auto invalid = expectedTransform;
                invalid.clipFromLocal[7] = value;
                check(!pipeline.recordDraw(commandToken, {640, 480}, invalid) && recorded.empty(), "Nonfinite matrix reached Vulkan");
                ++cases;
            }
        }
        check(livePipelines.empty() && liveLayouts.empty(), "Pipeline/layout destruction leaked resources");
    }
    expectedFormat = VK_FORMAT_R8G8B8A8_UNORM;
    expectedDepthFormat = VK_FORMAT_D16_UNORM;
    {
        VulkanTrianglePipeline pipeline;
        check(pipeline.initialize(device, expectedFormat, vertex, fragment, vertices, indices, expectedDepthFormat) &&
            pipeline.colorFormat() == expectedFormat && pipeline.depthFormat() == expectedDepthFormat, "New color/depth format was not used during rebuild");
        ++cases;
    }
    check(livePipelines.empty() && liveLayouts.empty(), "Rebuilt format leaked resources");
    for (const auto& invalid : {madcat::rhi::BufferRef{}, madcat::rhi::createVertexBuffer(device, std::as_bytes(std::span(geometry).first(1)))})
    {
        VulkanTrianglePipeline pipeline;
        const auto before = layoutCreates;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment, invalid, indices, expectedDepthFormat) && layoutCreates == before, "Invalid geometry accepted");
        ++cases;
    }
    // Reject malformed geometry before any pipeline objects or commands exist.
    const uint32_t outOfRange[] = {0, 1, 3};
    const uint32_t restart[] = {0, 1, UINT32_MAX};
    const uint32_t incomplete[] = {0, 1};
    for (const auto& invalid : {madcat::rhi::BufferRef{}, vertices,
        madcat::rhi::createIndexBuffer(device, outOfRange), madcat::rhi::createIndexBuffer(device, restart),
        madcat::rhi::createIndexBuffer(device, incomplete)})
    {
        VulkanTrianglePipeline pipeline;
        const auto before = layoutCreates;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment, vertices, invalid, expectedDepthFormat) &&
            layoutCreates == before && !pipeline.indexBuffer() && pipeline.indexCount() == 0, "Invalid indices acquired a pipeline");
        ++cases;
    }
    for (const auto& invalid : {indices, madcat::rhi::createVertexBuffer(device, std::as_bytes(std::span(geometry)).first(sizeof(geometry) - 1))})
    {
        VulkanTrianglePipeline pipeline;
        const auto before = layoutCreates;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment, invalid, indices, expectedDepthFormat) &&
            layoutCreates == before, "Wrong vertex usage/stride acquired a pipeline");
        ++cases;
    }
    // Accept a different vertex count and index count; DrawIndexed uses the
    // actual uploaded count rather than the old hard-coded three vertices.
    {
        const madcat::rhi::ColoredVertex quad[] = {geometry[0], geometry[1], geometry[2], geometry[0]};
        const uint32_t otherIndices[] = {3, 2, 1};
        auto otherVertices = madcat::rhi::createVertexBuffer(device, std::as_bytes(std::span(quad)));
        auto other = madcat::rhi::createIndexBuffer(device, otherIndices);
        expectedVertices = madcat::rhi::detail::BufferAccess::handle(otherVertices);
        expectedIndices = madcat::rhi::detail::BufferAccess::handle(other);
        expectedIndexCount = 3;
        VulkanTrianglePipeline pipeline;
        check(pipeline.initialize(device, expectedFormat, vertex, fragment, otherVertices, other, expectedDepthFormat) &&
            pipeline.indexCount() == 3, "Variable vertex/index count rejected");
        recorded.clear();
        check(pipeline.recordDraw(commandToken, expectedExtent, expectedTransform) && recorded == "BVSGIPD", "Variable count DrawIndexed failed");
        ++cases;
    }
    madcat::rhi::BufferRef frameReference, indexFrameReference;
    VkBuffer retained = VK_NULL_HANDLE;
    VkBuffer retainedIndices = VK_NULL_HANDLE;
    {
        auto uploaded = madcat::rhi::createVertexBuffer(device, std::as_bytes(std::span(geometry)));
        retained = madcat::rhi::detail::BufferAccess::handle(uploaded);
        auto indexUpload = madcat::rhi::createIndexBuffer(device, geometryIndices);
        retainedIndices = madcat::rhi::detail::BufferAccess::handle(indexUpload);
        VulkanTrianglePipeline pipeline;
        check(pipeline.initialize(device, expectedFormat, vertex, fragment, uploaded, indexUpload, expectedDepthFormat), "Retention setup failed");
        uploaded = {};
        indexUpload = {};
        check(liveBuffers.contains(retained) && liveBuffers.contains(retainedIndices), "Pipeline failed to retain geometry");
        frameReference = pipeline.vertexBuffer();
        indexFrameReference = pipeline.indexBuffer();
    }
    check(liveBuffers.contains(retained) && liveBuffers.contains(retainedIndices), "Frame references failed to outlive pipeline");
    frameReference = {};
    check(liveBuffers.contains(retainedIndices), "Vertex release also released indices");
    indexFrameReference = {};
    check(!liveBuffers.contains(retained) && !liveBuffers.contains(retainedIndices), "Frame references leaked geometry");
    ++cases;
    for (const auto invalidDepth : {VK_FORMAT_UNDEFINED, VK_FORMAT_R8_UNORM})
    {
        VulkanTrianglePipeline pipeline;
        const auto before = layoutCreates;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment, vertices, indices, invalidDepth) && layoutCreates == before,
            "Invalid depth format acquired pipeline objects");
        ++cases;
    }
}
}

// This target intentionally does not link GLFW or a Vulkan loader. All Vulkan
// calls are process-local mocks; no synthetic bytecode reaches a real driver.
GLFWAPI GLFWvkproc glfwGetInstanceProcAddress(VkInstance instance, const char* name)
{
    if (instance != instanceToken) { std::fprintf(stderr, "Wrong Instance for lookup.\n"); std::abort(); }
    if (name == missingFunction) { return nullptr; }
    if (std::strcmp(name, "vkGetPhysicalDeviceFormatProperties") == 0) { return reinterpret_cast<GLFWvkproc>(formatProperties); }
    if (std::strcmp(name, "vkGetPhysicalDeviceImageFormatProperties") == 0) { return reinterpret_cast<GLFWvkproc>(imageProperties); }
    if (std::strcmp(name, "vkGetPhysicalDeviceMemoryProperties") == 0) { return reinterpret_cast<GLFWvkproc>(memoryProperties); }
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) { return reinterpret_cast<GLFWvkproc>(deviceProc); }
    if (std::strcmp(name, "vkCreateDevice") == 0) { return reinterpret_cast<GLFWvkproc>(createDevice); }
    if (std::strcmp(name, "vkDestroyDevice") == 0) { return reinterpret_cast<GLFWvkproc>(destroyDevice); }
    std::fprintf(stderr, "Unexpected Instance function lookup: %s.\n", name);
    std::abort();
}

int main(int argc, char** argv)
{
    try
    {
        check(argc == 2 || (argc == 3 && (std::string_view(argv[2]) == "triangle" || std::string_view(argv[2]) == "buffer" || std::string_view(argv[2]) == "depth")),
            "Expected output directory and optional triangle/buffer/depth mode");
        const auto root = std::filesystem::absolute(std::filesystem::path(argv[1]));
        std::filesystem::create_directories(root);
        if (argc == 3 && std::string_view(argv[2]) == "depth")
        {
            depthChecks();
            std::printf("Depth texture checks: %d cases passed (mocked Vulkan).\n", cases);
        }
        else if (argc == 3 && std::string_view(argv[2]) == "buffer")
        {
            bufferChecks();
            indexBufferChecks();
            std::printf("Buffer checks: %d cases passed (mocked Vulkan).\n", cases);
        }
        else if (argc == 3)
        {
            triangleChecks(root);
            std::printf("Triangle pipeline checks: %d cases passed (mocked Vulkan).\n", cases);
        }
        else
        {
            loaderChecks(root);
            moduleChecks(root);
            std::printf("Shader Module checks: %d cases passed (CPU and mocked Vulkan).\n", cases);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Shader Module checks failed: %s\n", error.what());
        return 1;
    }
}
