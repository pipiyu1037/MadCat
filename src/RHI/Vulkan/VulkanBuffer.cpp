#include "VulkanBuffer.h"

#include "VulkanDevice.h"
#include "VulkanFunctions.h"

#include <cstring>
#include <algorithm>
#include <type_traits>
#include <utility>

namespace
{
const char* bufferName(VkBufferUsageFlags usage)
{
    if (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) { return "Index"; }
    if (usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) { return "Vertex"; }
    if (usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) { return "AS storage"; }
    if (usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR) { return "AS instances"; }
    return "Storage";
}
constexpr VkBufferUsageFlags rayQueryInputUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
}

namespace madcat::rhi
{
struct detail::BufferState
{
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::size_t size = 0;
    VkBufferUsageFlags usage = 0;
    VkDeviceAddress address = 0;
    uint32_t maximumIndex = 0;
    uint32_t memoryType = 0;
    bool hostVisible = false;
    const char* label = "Storage";
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;

    ~BufferState()
    {
        // Callers retain a reference until all submitted reads/writes are complete.
        if (buffer) { destroyBuffer(device, buffer, nullptr); }
        if (memory) { freeMemory(device, memory, nullptr); }
        if (buffer) { std::fprintf(stderr, "Vulkan: %s buffer destroyed.\n", label); }
    }
};

std::size_t BufferRef::sizeBytes() const noexcept { return state_ ? state_->size : 0; }
uint64_t BufferRef::deviceAddress() const noexcept { return state_ ? state_->address : 0; }
VkBuffer detail::BufferAccess::handle(const BufferRef& buffer) noexcept
{
    return buffer.state_ ? buffer.state_->buffer : VK_NULL_HANDLE;
}
VkDevice detail::BufferAccess::device(const BufferRef& buffer) noexcept
{
    return buffer.state_ ? buffer.state_->device : VK_NULL_HANDLE;
}

VkBufferUsageFlags detail::BufferAccess::usage(const BufferRef& buffer) noexcept
{
    return buffer.state_ ? buffer.state_->usage : 0;
}
uint32_t detail::BufferAccess::maximumIndex(const BufferRef& buffer) noexcept
{
    return buffer.state_ ? buffer.state_->maximumIndex : 0;
}
bool detail::BufferAccess::read(const BufferRef& buffer,std::span<std::byte> destination)
{
    if (!buffer.state_ || !buffer.state_->hostVisible || destination.size()!=buffer.sizeBytes()) { return false; }
    auto& s=*buffer.state_;
    void* mapped=nullptr;
    if (!detail::checkResult(s.mapMemory(s.device,s.memory,0,s.size,0,&mapped),"map completed buffer")) { return false; }
    std::memcpy(destination.data(),mapped,s.size);
    s.unmapMemory(s.device,s.memory); return true;
}
bool detail::BufferAccess::write(const BufferRef& buffer,std::span<const std::byte> data)
{
    if (!buffer.state_ || !buffer.state_->hostVisible || data.size()>buffer.sizeBytes()) { return false; }
    if (data.empty()) { return true; }
    auto& s=*buffer.state_;
    void* mapped=nullptr;
    if (!detail::checkResult(s.mapMemory(s.device,s.memory,0,data.size(),0,&mapped),"map buffer upload")) { return false; }
    std::memcpy(mapped,data.data(),data.size()); s.unmapMemory(s.device,s.memory); return true;
}

namespace
{
std::shared_ptr<detail::BufferState> allocateBuffer(const VulkanDevice& device, size_t size, VkBufferUsageFlags usage, bool hostVisible)
{
    if (!device.handle() || !size)
    {
        std::fprintf(stderr, "Vulkan error: buffer upload requires an initialized Device and nonempty data.\n");
        return {};
    }
    const bool addressable = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;
    if (addressable && !device.rayQueryEnabled())
    {
        std::fprintf(stderr, "Vulkan error: addressable buffer requires an enabled Ray Query Device.\n");
        return {};
    }
    const auto instance = device.physicalDeviceInfo().instance;
    const auto getMemoryProperties = detail::loadFunction<PFN_vkGetPhysicalDeviceMemoryProperties>(instance, "vkGetPhysicalDeviceMemoryProperties");
    const auto getProc = detail::loadFunction<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
    if (!getMemoryProperties || !getProc) { return {}; }
    auto state = std::make_shared<detail::BufferState>();
    state->device = device.handle();
    state->size = size;
    state->usage = usage;
    state->hostVisible=hostVisible;
    state->label=hostVisible || (usage&VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) ? bufferName(usage) : "AS scratch";
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements getRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkBindBufferMemory bindMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
    PFN_vkGetBufferDeviceAddress getAddress = nullptr;
    const auto load = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(getProc(state->device, name));
        if (!function) { std::fprintf(stderr, "Vulkan error: required device function %s is unavailable.\n", name); }
        return function != nullptr;
    };
    // All cleanup operations must exist before acquiring any GPU resources.
    if (!load(createBuffer, "vkCreateBuffer") || !load(state->destroyBuffer, "vkDestroyBuffer") ||
        !load(getRequirements, "vkGetBufferMemoryRequirements") || !load(allocateMemory, "vkAllocateMemory") ||
        !load(state->freeMemory, "vkFreeMemory") || !load(bindMemory, "vkBindBufferMemory") ||
        !load(mapMemory, "vkMapMemory") || !load(unmapMemory, "vkUnmapMemory") ||
        (addressable && !load(getAddress, "vkGetBufferDeviceAddress"))) { return {}; }
    state->mapMemory=mapMemory; state->unmapMemory=unmapMemory;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer = VK_NULL_HANDLE;
    if (!detail::checkResult(createBuffer(state->device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer")) { return {}; }
    state->buffer = buffer;
    VkMemoryRequirements requirements{};
    getRequirements(state->device, buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    getMemoryProperties(device.physicalDevice(), &properties);
    const VkMemoryPropertyFlags required = hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
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
        std::fprintf(stderr, "Vulkan error: buffer has no compatible memory type for flags 0x%x.\n",required);
        return {};
    }
    state->memoryType=memoryType;
    VkMemoryDedicatedAllocateInfo dedicated{};
    dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated.buffer = buffer;
    VkMemoryAllocateFlagsInfo flags{};
    flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    if (addressable) { dedicated.pNext = &flags; }
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.pNext = &dedicated;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (!detail::checkResult(allocateMemory(state->device, &allocation, nullptr, &memory), "vkAllocateMemory")) { return {}; }
    state->memory = memory;
    if (!detail::checkResult(bindMemory(state->device, buffer, memory, 0), "vkBindBufferMemory")) { return {}; }
    if (addressable)
    {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer;
        state->address = getAddress(state->device, &addressInfo);
        if (!state->address)
        {
            std::fprintf(stderr, "Vulkan error: vkGetBufferDeviceAddress returned zero.\n");
            return {};
        }
    }
    return state;
}
std::shared_ptr<detail::BufferState> uploadBuffer(const VulkanDevice& device,std::span<const std::byte> data,VkBufferUsageFlags usage)
{
    auto state=allocateBuffer(device,data.size(),usage,true);
    if (!state) { return {}; }
    void* mapped=nullptr;
    if (!detail::checkResult(state->mapMemory(state->device,state->memory,0,data.size(),0,&mapped),"vkMapMemory")) { return {}; }
    std::memcpy(mapped,data.data(),data.size()); state->unmapMemory(state->device,state->memory);
    // Coherent host writes complete before queue submission; the submit performs
    // the host-to-device domain operation. No transfer command/barrier is needed.
    std::fprintf(stderr, "Vulkan: %s buffer uploaded; %zu bytes, coherent memory type %u, usage 0x%x, device address 0x%llx.\n",
        state->label, data.size(), state->memoryType, usage, static_cast<unsigned long long>(state->address));
    return state;
}
}

BufferRef detail::BufferAccess::allocate(const VulkanDevice& device,size_t bytes,VkBufferUsageFlags usage,bool hostVisible)
{
    BufferRef result; result.state_=allocateBuffer(device,bytes,usage,hostVisible);
    if (result) { std::fprintf(stderr,"Vulkan: %s buffer allocated; %zu bytes.\n",result.state_->label,bytes); }
    return result;
}

BufferRef createVertexBuffer(const VulkanDevice& device, std::span<const std::byte> data, bool rayQueryInput)
{
    BufferRef result;
    result.state_ = uploadBuffer(device, data, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | (rayQueryInput ? rayQueryInputUsage : 0));
    return result;
}

BufferRef createIndexBuffer(const VulkanDevice& device, std::span<const uint32_t> data, bool rayQueryInput)
{
    if (data.empty() || data.size() > UINT32_MAX)
    {
        std::fprintf(stderr, "Vulkan error: index upload requires a nonempty uint32 index count.\n");
        return {};
    }
    const auto maximum = *std::max_element(data.begin(), data.end());
    // The current Device leaves fullDrawIndexUint32 disabled. Use the guaranteed
    // 24-bit range and the queried limit, even on hardware advertising more.
    if (device.handle() && maximum > std::min(device.physicalDeviceInfo().properties.limits.maxDrawIndexedIndexValue, uint32_t{0x00ffffff}))
    {
        std::fprintf(stderr, "Vulkan error: index value exceeds the current Device draw-index limit.\n");
        return {};
    }
    BufferRef result;
    result.state_ = uploadBuffer(device, std::as_bytes(data), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | (rayQueryInput ? rayQueryInputUsage : 0));
    if (result.state_) { result.state_->maximumIndex = maximum; }
    return result;
}

BufferRef createStorageBuffer(const VulkanDevice& device, std::span<const std::byte> data, bool deviceAddress)
{
    BufferRef result;
    result.state_ = uploadBuffer(device, data, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        (deviceAddress ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : 0));
    return result;
}
}
