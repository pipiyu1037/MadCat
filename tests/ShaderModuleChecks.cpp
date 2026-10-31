#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "RHI/Vulkan/VulkanDevice.h"
#include "RHI/Vulkan/VulkanBuffer.h"
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
VkFormat expectedFormat = VK_FORMAT_B8G8R8A8_SRGB;
VkShaderModule expectedVertex = VK_NULL_HANDLE;
VkShaderModule expectedFragment = VK_NULL_HANDLE;
int layoutCreates = 0, pipelineCreates = 0;
const auto commandToken = reinterpret_cast<VkCommandBuffer>(6);
std::set<VkBuffer> liveBuffers;
std::map<VkDeviceMemory, std::vector<std::byte>> liveMemory;
std::map<VkBuffer, VkDeviceMemory> boundMemory;
std::set<VkDeviceMemory> mappedMemory;
std::string_view failingBufferOperation;
uint32_t memoryTypeBits = 2;
bool noCoherentMemory = false;
int bufferCreates = 0;
constexpr madcat::rhi::TriangleVertex geometry[] = {
    {{0, -0.6f}, {1, 0, 0}}, {{0.6f, 0.6f}, {0, 1, 0}}, {{-0.6f, 0.6f}, {0, 0, 1}}
};

void writeWords(const std::filesystem::path& file, const std::vector<uint32_t>& words)
{
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(words.data()), static_cast<std::streamsize>(words.size() * sizeof(uint32_t)));
    check(static_cast<bool>(output), "Could not write test fixture");
}

VKAPI_ATTR VkResult VKAPI_CALL createDevice(VkPhysicalDevice, const VkDeviceCreateInfo*, const VkAllocationCallbacks*, VkDevice* device)
{
    *device = deviceToken;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice, const VkAllocationCallbacks*)
{
    check(liveModules.empty(), "Device destroyed before its modules");
    check(liveLayouts.empty() && livePipelines.empty(), "Device destroyed before pipeline/layout");
    check(liveBuffers.empty() && liveMemory.empty(), "Device destroyed before buffer/memory");
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
    properties->memoryHeaps[0].size = 4096;
}
VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice device, const VkBufferCreateInfo* info,
    const VkAllocationCallbacks* allocator, VkBuffer* buffer)
{
    ++bufferCreates;
    check(device == deviceToken && !allocator && info->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && !info->pNext &&
        info->size > 0 && info->size <= 256 && info->flags == 0 && info->usage == VK_BUFFER_USAGE_VERTEX_BUFFER_BIT &&
        info->sharingMode == VK_SHARING_MODE_EXCLUSIVE && info->queueFamilyIndexCount == 0, "Wrong buffer creation parameters");
    *buffer = reinterpret_cast<VkBuffer>(9999);
    if (failingBufferOperation == "create") { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *buffer = reinterpret_cast<VkBuffer>(++serial);
    liveBuffers.insert(*buffer);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* allocator)
{
    check(device == deviceToken && !allocator && liveBuffers.erase(buffer) == 1, "Invalid or repeated buffer destruction");
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
        info->allocationSize == 256 && info->memoryTypeIndex == 1 && dedicated &&
        dedicated->sType == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO && !dedicated->pNext &&
        liveBuffers.contains(dedicated->buffer) && !dedicated->image, "Wrong memory allocation or type selection");
    *memory = reinterpret_cast<VkDeviceMemory>(9999);
    if (failingBufferOperation == "allocate") { return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    *memory = reinterpret_cast<VkDeviceMemory>(++serial);
    liveMemory.emplace(*memory, std::vector<std::byte>(256));
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* allocator)
{
    check(device == deviceToken && !allocator && !mappedMemory.contains(memory), "Invalid free or memory still mapped");
    for (const auto& [buffer, bound] : boundMemory) { check(bound != memory, "Memory freed before buffer"); (void)buffer; }
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
        info->pNext == nullptr && info->flags == 0 && info->setLayoutCount == 0 && info->pushConstantRangeCount == 0, "Incorrect empty pipeline layout");
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
        rendering->depthAttachmentFormat == VK_FORMAT_UNDEFINED && rendering->stencilAttachmentFormat == VK_FORMAT_UNDEFINED, "Wrong dynamic-rendering formats");
    const auto& input = *info->pVertexInputState;
    check(input.vertexBindingDescriptionCount == 1 && input.vertexAttributeDescriptionCount == 2 &&
        input.pVertexBindingDescriptions[0].binding == 0 && input.pVertexBindingDescriptions[0].stride == 20 &&
        input.pVertexBindingDescriptions[0].inputRate == VK_VERTEX_INPUT_RATE_VERTEX &&
        input.pVertexAttributeDescriptions[0].location == 0 && input.pVertexAttributeDescriptions[0].binding == 0 &&
        input.pVertexAttributeDescriptions[0].format == VK_FORMAT_R32G32_SFLOAT && input.pVertexAttributeDescriptions[0].offset == 0 &&
        input.pVertexAttributeDescriptions[1].location == 1 && input.pVertexAttributeDescriptions[1].binding == 0 &&
        input.pVertexAttributeDescriptions[1].format == VK_FORMAT_R32G32B32_SFLOAT && input.pVertexAttributeDescriptions[1].offset == 8,
        "Wrong vertex buffer layout");
    check(info->pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && info->pInputAssemblyState->primitiveRestartEnable == VK_FALSE &&
        info->pRasterizationState->polygonMode == VK_POLYGON_MODE_FILL && info->pRasterizationState->cullMode == VK_CULL_MODE_NONE &&
        info->pRasterizationState->lineWidth == 1.0f && info->pRasterizationState->rasterizerDiscardEnable == VK_FALSE &&
        info->pMultisampleState->rasterizationSamples == VK_SAMPLE_COUNT_1_BIT && info->pDepthStencilState->depthTestEnable == VK_FALSE &&
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
VKAPI_ATTR void VKAPI_CALL draw(VkCommandBuffer command, uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance)
{
    check(command == commandToken && recorded == "BVSG" && vertices == 3 && instances == 1 && firstVertex == 0 && firstInstance == 0, "Wrong Draw arguments/order");
    recorded += 'D';
}
VKAPI_ATTR void VKAPI_CALL bindVertices(VkCommandBuffer command, uint32_t first, uint32_t count,
    const VkBuffer* buffers, const VkDeviceSize* offsets)
{
    check(command == commandToken && recorded == "BVS" && first == 0 && count == 1 && offsets[0] == 0 &&
        liveBuffers.contains(buffers[0]) && boundMemory.contains(buffers[0]), "Wrong vertex buffer binding/order");
    recorded += 'G';
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice device, const char* name)
{
    check(device == deviceToken, "Function lookup used wrong Device");
    if (name == missingFunction) { return nullptr; }
    if (std::strcmp(name, "vkCreateBuffer") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(createBuffer); }
    if (std::strcmp(name, "vkDestroyBuffer") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(destroyBuffer); }
    if (std::strcmp(name, "vkGetBufferMemoryRequirements") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(getRequirements); }
    if (std::strcmp(name, "vkAllocateMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(allocateMemory); }
    if (std::strcmp(name, "vkFreeMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(freeMemory); }
    if (std::strcmp(name, "vkBindBufferMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(bindMemory); }
    if (std::strcmp(name, "vkMapMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(mapMemory); }
    if (std::strcmp(name, "vkUnmapMemory") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(unmapMemory); }
    if (std::strcmp(name, "vkCmdBindVertexBuffers") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(bindVertices); }
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
    if (std::strcmp(name, "vkCmdDraw") == 0) { return reinterpret_cast<PFN_vkVoidFunction>(draw); }
    throw std::runtime_error("Unexpected device function lookup");
}

madcat::rhi::PhysicalDeviceCandidate candidate()
{
    madcat::rhi::PhysicalDeviceCandidate result;
    auto& device = result.device;
    device.instance = instanceToken;
    device.handle = reinterpret_cast<VkPhysicalDevice>(4);
    device.properties.apiVersion = VK_API_VERSION_1_3;
    device.features.dynamicRendering = device.features.synchronization2 = {true, true};
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

void triangleChecks(const std::filesystem::path& root)
{
    using madcat::rhi::VulkanTrianglePipeline;
    static_assert(!std::is_copy_constructible_v<VulkanTrianglePipeline> && !std::is_move_constructible_v<VulkanTrianglePipeline>);
    const auto file = root / "module.spv";
    writeWords(file, fixture);
    madcat::rhi::VulkanDevice device;
    madcat::rhi::VulkanShaderModule vertex, fragment, empty;
    madcat::rhi::BufferRef vertices;
    {
        VulkanTrianglePipeline pipeline;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment, vertices), "Uninitialized Device accepted");
        check(!pipeline.recordDraw(commandToken, {640, 480}), "Uninitialized pipeline recorded Draw");
        cases += 2;
    }
    const auto input = candidate();
    check(device.initialize({&input, 1}) && vertex.initialize(device, file) && fragment.initialize(device, file), "Mock setup failed");
    vertices = madcat::rhi::createVertexBuffer(device, std::as_bytes(std::span(geometry)));
    check(static_cast<bool>(vertices), "Mock upload failed");
    expectedVertex = vertex.handle();
    expectedFragment = fragment.handle();
    for (int invalid = 0; invalid < 3; ++invalid)
    {
        VulkanTrianglePipeline pipeline;
        check(!pipeline.initialize(device, invalid == 2 ? VK_FORMAT_UNDEFINED : expectedFormat,
            invalid == 0 ? empty : vertex, invalid == 1 ? empty : fragment, vertices), "Invalid module/format accepted");
        check(layoutCreates == 0, "Invalid input reached Vulkan");
        ++cases;
    }
    for (const auto* name : {"vkGetDeviceProcAddr", "vkCreatePipelineLayout", "vkDestroyPipelineLayout", "vkCreateGraphicsPipelines",
        "vkDestroyPipeline", "vkCmdBindPipeline", "vkCmdSetViewport", "vkCmdSetScissor", "vkCmdDraw", "vkCmdBindVertexBuffers"})
    {
        missingFunction = name;
        VulkanTrianglePipeline pipeline;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment, vertices) && pipeline.handle() == VK_NULL_HANDLE &&
            pipeline.deviceHandle() == VK_NULL_HANDLE && pipeline.colorFormat() == VK_FORMAT_UNDEFINED && layoutCreates == 0, "Missing function acquired pipeline resources");
        ++cases;
    }
    missingFunction = {};
    for (const auto* operation : {"layout", "pipeline"})
    {
        {
            VulkanTrianglePipeline pipeline;
            failingPipelineOperation = operation;
            check(!pipeline.initialize(device, expectedFormat, vertex, fragment, vertices) && pipeline.handle() == VK_NULL_HANDLE &&
                liveLayouts.empty() && livePipelines.empty(), "Failed creation leaked a pipeline/layout");
            ++cases;
            failingPipelineOperation = {};
            check(pipeline.initialize(device, expectedFormat, vertex, fragment, vertices), "Pipeline creation retry failed");
            const auto handle = pipeline.handle();
            const auto before = pipelineCreates;
            check(pipeline.deviceHandle() == device.handle() && pipeline.colorFormat() == expectedFormat &&
                !pipeline.initialize(device, expectedFormat, vertex, fragment, vertices) && pipeline.handle() == handle && pipelineCreates == before, "Repeated creation changed live pipeline");
            ++cases;
            for (VkExtent2D extent : {VkExtent2D{640, 480}, VkExtent2D{937, 611}})
            {
                recorded.clear();
                expectedExtent = extent;
                check(pipeline.recordDraw(commandToken, extent) && recorded == "BVSGD", "Wrong recording sequence");
                ++cases;
            }
            recorded.clear();
            check(!pipeline.recordDraw(VK_NULL_HANDLE, {640, 480}) && !pipeline.recordDraw(commandToken, {0, 480}) &&
                !pipeline.recordDraw(commandToken, {640, 0}) && recorded.empty(), "Invalid recording input reached Vulkan");
            ++cases;
        }
        check(livePipelines.empty() && liveLayouts.empty(), "Pipeline/layout destruction leaked resources");
    }
    expectedFormat = VK_FORMAT_R8G8B8A8_UNORM;
    {
        VulkanTrianglePipeline pipeline;
        check(pipeline.initialize(device, expectedFormat, vertex, fragment, vertices) && pipeline.colorFormat() == expectedFormat, "New color format was not used during rebuild");
        ++cases;
    }
    check(livePipelines.empty() && liveLayouts.empty(), "Rebuilt format leaked resources");
    for (const auto& invalid : {madcat::rhi::BufferRef{}, madcat::rhi::createVertexBuffer(device, std::as_bytes(std::span(geometry).first(1)))})
    {
        VulkanTrianglePipeline pipeline;
        const auto before = layoutCreates;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment, invalid) && layoutCreates == before, "Invalid geometry accepted");
        ++cases;
    }
    madcat::rhi::BufferRef frameReference;
    VkBuffer retained = VK_NULL_HANDLE;
    {
        auto uploaded = madcat::rhi::createVertexBuffer(device, std::as_bytes(std::span(geometry)));
        retained = madcat::rhi::detail::BufferAccess::handle(uploaded);
        VulkanTrianglePipeline pipeline;
        check(pipeline.initialize(device, expectedFormat, vertex, fragment, uploaded), "Retention setup failed");
        uploaded = {};
        check(liveBuffers.contains(retained), "Pipeline failed to retain vertices");
        frameReference = pipeline.vertexBuffer();
    }
    check(liveBuffers.contains(retained), "Frame reference failed to outlive pipeline");
    frameReference = {};
    check(!liveBuffers.contains(retained), "Frame reference leaked buffer");
    ++cases;
}
}

// This target intentionally does not link GLFW or a Vulkan loader. All Vulkan
// calls are process-local mocks; no synthetic bytecode reaches a real driver.
GLFWAPI GLFWvkproc glfwGetInstanceProcAddress(VkInstance instance, const char* name)
{
    if (instance != instanceToken) { std::fprintf(stderr, "Wrong Instance for lookup.\n"); std::abort(); }
    if (name == missingFunction) { return nullptr; }
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
        check(argc == 2 || (argc == 3 && (std::string_view(argv[2]) == "triangle" || std::string_view(argv[2]) == "buffer")),
            "Expected output directory and optional triangle/buffer mode");
        const auto root = std::filesystem::absolute(std::filesystem::path(argv[1]));
        std::filesystem::create_directories(root);
        if (argc == 3 && std::string_view(argv[2]) == "buffer")
        {
            bufferChecks();
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
