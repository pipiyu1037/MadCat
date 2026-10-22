#define GLFW_INCLUDE_NONE
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include "RHI/Vulkan/VulkanDevice.h"
#include "RHI/Vulkan/VulkanShaderModule.h"
#include "RHI/Vulkan/VulkanTrianglePipeline.h"
#include "Shader/Spirv.h"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
}
VKAPI_ATTR VkResult VKAPI_CALL waitIdle(VkDevice) { return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL getQueue(VkDevice, uint32_t, uint32_t, VkQueue* queue) { *queue = reinterpret_cast<VkQueue>(3); }

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
    check(info->pVertexInputState->vertexBindingDescriptionCount == 0 && info->pVertexInputState->vertexAttributeDescriptionCount == 0 &&
        info->pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && info->pInputAssemblyState->primitiveRestartEnable == VK_FALSE &&
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
    check(command == commandToken && recorded == "BVS" && vertices == 3 && instances == 1 && firstVertex == 0 && firstInstance == 0, "Wrong Draw arguments/order");
    recorded += 'D';
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice device, const char* name)
{
    check(device == deviceToken, "Function lookup used wrong Device");
    if (name == missingFunction) { return nullptr; }
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

void triangleChecks(const std::filesystem::path& root)
{
    using madcat::rhi::VulkanTrianglePipeline;
    static_assert(!std::is_copy_constructible_v<VulkanTrianglePipeline> && !std::is_move_constructible_v<VulkanTrianglePipeline>);
    const auto file = root / "module.spv";
    writeWords(file, fixture);
    madcat::rhi::VulkanDevice device;
    madcat::rhi::VulkanShaderModule vertex, fragment, empty;
    {
        VulkanTrianglePipeline pipeline;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment), "Uninitialized Device accepted");
        check(!pipeline.recordDraw(commandToken, {640, 480}), "Uninitialized pipeline recorded Draw");
        cases += 2;
    }
    const auto input = candidate();
    check(device.initialize({&input, 1}) && vertex.initialize(device, file) && fragment.initialize(device, file), "Mock setup failed");
    expectedVertex = vertex.handle();
    expectedFragment = fragment.handle();
    for (int invalid = 0; invalid < 3; ++invalid)
    {
        VulkanTrianglePipeline pipeline;
        check(!pipeline.initialize(device, invalid == 2 ? VK_FORMAT_UNDEFINED : expectedFormat,
            invalid == 0 ? empty : vertex, invalid == 1 ? empty : fragment), "Invalid module/format accepted");
        check(layoutCreates == 0, "Invalid input reached Vulkan");
        ++cases;
    }
    for (const auto* name : {"vkGetDeviceProcAddr", "vkCreatePipelineLayout", "vkDestroyPipelineLayout", "vkCreateGraphicsPipelines",
        "vkDestroyPipeline", "vkCmdBindPipeline", "vkCmdSetViewport", "vkCmdSetScissor", "vkCmdDraw"})
    {
        missingFunction = name;
        VulkanTrianglePipeline pipeline;
        check(!pipeline.initialize(device, expectedFormat, vertex, fragment) && pipeline.handle() == VK_NULL_HANDLE &&
            pipeline.deviceHandle() == VK_NULL_HANDLE && pipeline.colorFormat() == VK_FORMAT_UNDEFINED && layoutCreates == 0, "Missing function acquired pipeline resources");
        ++cases;
    }
    missingFunction = {};
    for (const auto* operation : {"layout", "pipeline"})
    {
        {
            VulkanTrianglePipeline pipeline;
            failingPipelineOperation = operation;
            check(!pipeline.initialize(device, expectedFormat, vertex, fragment) && pipeline.handle() == VK_NULL_HANDLE &&
                liveLayouts.empty() && livePipelines.empty(), "Failed creation leaked a pipeline/layout");
            ++cases;
            failingPipelineOperation = {};
            check(pipeline.initialize(device, expectedFormat, vertex, fragment), "Pipeline creation retry failed");
            const auto handle = pipeline.handle();
            const auto before = pipelineCreates;
            check(pipeline.deviceHandle() == device.handle() && pipeline.colorFormat() == expectedFormat &&
                !pipeline.initialize(device, expectedFormat, vertex, fragment) && pipeline.handle() == handle && pipelineCreates == before, "Repeated creation changed live pipeline");
            ++cases;
            for (VkExtent2D extent : {VkExtent2D{640, 480}, VkExtent2D{937, 611}})
            {
                recorded.clear();
                expectedExtent = extent;
                check(pipeline.recordDraw(commandToken, extent) && recorded == "BVSD", "Wrong recording sequence");
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
        check(pipeline.initialize(device, expectedFormat, vertex, fragment) && pipeline.colorFormat() == expectedFormat, "New color format was not used during rebuild");
        ++cases;
    }
    check(livePipelines.empty() && liveLayouts.empty(), "Rebuilt format leaked resources");
}
}

// This target intentionally does not link GLFW or a Vulkan loader. All Vulkan
// calls are process-local mocks; no synthetic bytecode reaches a real driver.
GLFWAPI GLFWvkproc glfwGetInstanceProcAddress(VkInstance instance, const char* name)
{
    if (instance != instanceToken) { std::fprintf(stderr, "Wrong Instance for lookup.\n"); std::abort(); }
    if (name == missingFunction) { return nullptr; }
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
        check(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "triangle"), "Expected output directory and optional triangle mode");
        const auto root = std::filesystem::absolute(std::filesystem::path(argv[1]));
        std::filesystem::create_directories(root);
        if (argc == 3)
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
