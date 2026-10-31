#include "VulkanBuffer.h"

#include "VulkanDevice.h"
#include "VulkanFunctions.h"

#include <cstring>
#include <type_traits>
#include <utility>

namespace madcat::rhi
{
struct detail::BufferState
{
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::size_t size = 0;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;

    ~BufferState()
    {
        // Callers retain a reference until submitted reads are complete.
        if (buffer) { destroyBuffer(device, buffer, nullptr); }
        if (memory) { freeMemory(device, memory, nullptr); }
        if (buffer) { std::fprintf(stderr, "Vulkan: Vertex buffer destroyed.\n"); }
    }
};

std::size_t BufferRef::sizeBytes() const noexcept { return state_ ? state_->size : 0; }
VkBuffer detail::BufferAccess::handle(const BufferRef& buffer) noexcept
{
    return buffer.state_ ? buffer.state_->buffer : VK_NULL_HANDLE;
}
VkDevice detail::BufferAccess::device(const BufferRef& buffer) noexcept
{
    return buffer.state_ ? buffer.state_->device : VK_NULL_HANDLE;
}

BufferRef createVertexBuffer(const VulkanDevice& device, std::span<const std::byte> data)
{
    if (!device.handle() || data.empty())
    {
        std::fprintf(stderr, "Vulkan error: vertex upload requires an initialized Device and nonempty data.\n");
        return {};
    }
    const auto instance = device.physicalDeviceInfo().instance;
    const auto getMemoryProperties = detail::loadFunction<PFN_vkGetPhysicalDeviceMemoryProperties>(instance, "vkGetPhysicalDeviceMemoryProperties");
    const auto getProc = detail::loadFunction<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
    if (!getMemoryProperties || !getProc) { return {}; }
    auto state = std::make_shared<detail::BufferState>();
    state->device = device.handle();
    state->size = data.size();
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements getRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkBindBufferMemory bindMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
    const auto load = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(getProc(state->device, name));
        if (!function) { std::fprintf(stderr, "Vulkan error: required device function %s is unavailable.\n", name); }
        return function != nullptr;
    };
    // All cleanup operations must exist before acquiring any GPU resources.
    if (!load(createBuffer, "vkCreateBuffer") || !load(state->destroyBuffer, "vkDestroyBuffer") ||
        !load(getRequirements, "vkGetBufferMemoryRequirements") || !load(allocateMemory, "vkAllocateMemory") ||
        !load(state->freeMemory, "vkFreeMemory") || !load(bindMemory, "vkBindBufferMemory") ||
        !load(mapMemory, "vkMapMemory") || !load(unmapMemory, "vkUnmapMemory")) { return {}; }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = data.size();
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer = VK_NULL_HANDLE;
    if (!detail::checkResult(createBuffer(state->device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer")) { return {}; }
    state->buffer = buffer;
    VkMemoryRequirements requirements{};
    getRequirements(state->device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    getMemoryProperties(device.physicalDevice(), &properties);
    constexpr auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
    {
        if ((requirements.memoryTypeBits & (uint32_t{1} << index)) != 0 &&
            (properties.memoryTypes[index].propertyFlags & required) == required)
        {
            memoryType = index;
            break;
        }
    }
    if (memoryType == UINT32_MAX)
    {
        std::fprintf(stderr, "Vulkan error: vertex buffer has no compatible HOST_VISIBLE | HOST_COHERENT memory type.\n");
        return {};
    }
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.buffer = buffer;
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = &dedicated;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (!detail::checkResult(allocateMemory(state->device, &allocation, nullptr, &memory), "vkAllocateMemory")) { return {}; }
    state->memory = memory;
    if (!detail::checkResult(bindMemory(state->device, buffer, memory, 0), "vkBindBufferMemory")) { return {}; }
    void* mapped = nullptr;
    if (!detail::checkResult(mapMemory(state->device, memory, 0, data.size(), 0, &mapped), "vkMapMemory")) { return {}; }
    std::memcpy(mapped, data.data(), data.size());
    unmapMemory(state->device, memory);
    // Coherent host writes complete before queue submission; the submit performs
    // the host-to-device domain operation. No transfer command/barrier is needed.
    BufferRef result;
    result.state_ = std::move(state);
    std::fprintf(stderr, "Vulkan: Vertex buffer uploaded; %zu bytes, coherent memory type %u.\n", data.size(), memoryType);
    return result;
}
}
