#include "VulkanFrameLighting.h"
#include "VulkanDevice.h"
#include "VulkanFunctions.h"

#include <cstring>
#include <type_traits>

namespace madcat::rhi::detail
{
struct VulkanFrameLighting::State
{
    VkDevice device = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
    void* mapped = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
    PFN_vkDestroyDescriptorPool destroyPool = nullptr;
    ~State()
    {
        if (pool) { destroyPool(device, pool, nullptr); }
        if (mapped) { unmapMemory(device, memory); }
        if (buffer) { destroyBuffer(device, buffer, nullptr); }
        if (memory) { freeMemory(device, memory, nullptr); }
        if (buffer) { std::fprintf(stderr, "Vulkan: Frame lighting buffer destroyed.\n"); }
    }
};
VulkanFrameLighting::VulkanFrameLighting() = default;
VulkanFrameLighting::~VulkanFrameLighting() = default;

bool VulkanFrameLighting::initialize(const VulkanDevice& device, VkDescriptorSetLayout layout)
{
    if (state_ || !device.handle() || !layout) { return false; }
    const auto instance = device.physicalDeviceInfo().instance;
    const auto getProc = loadFunction<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
    const auto getMemory = loadFunction<PFN_vkGetPhysicalDeviceMemoryProperties>(instance, "vkGetPhysicalDeviceMemoryProperties");
    if (!getProc || !getMemory) { return false; }
    auto created = std::make_unique<State>();
    created->device = device.handle();
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements getRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkBindBufferMemory bindMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkCreateDescriptorPool createPool = nullptr;
    PFN_vkAllocateDescriptorSets allocateSets = nullptr;
    PFN_vkUpdateDescriptorSets updateSets = nullptr;
    const auto load = [&](auto& function, const char* name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(getProc(created->device, name));
        if (!function) { std::fprintf(stderr, "Vulkan error: required device function %s is unavailable.\n", name); }
        return function != nullptr;
    };
    if (!load(createBuffer, "vkCreateBuffer") || !load(created->destroyBuffer, "vkDestroyBuffer") ||
        !load(getRequirements, "vkGetBufferMemoryRequirements") || !load(allocateMemory, "vkAllocateMemory") ||
        !load(created->freeMemory, "vkFreeMemory") || !load(bindMemory, "vkBindBufferMemory") ||
        !load(mapMemory, "vkMapMemory") || !load(created->unmapMemory, "vkUnmapMemory") ||
        !load(createPool, "vkCreateDescriptorPool") || !load(created->destroyPool, "vkDestroyDescriptorPool") ||
        !load(allocateSets, "vkAllocateDescriptorSets") || !load(updateSets, "vkUpdateDescriptorSets")) { return false; }

    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = sizeof(FrameLighting);
    buffer.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (!checkResult(createBuffer(created->device, &buffer, nullptr, &created->buffer), "vkCreateBuffer(lighting)")) { return false; }
    VkMemoryRequirements requirements{};
    getRequirements(created->device, created->buffer, &requirements);
    VkPhysicalDeviceMemoryProperties properties{};
    getMemory(device.physicalDeviceInfo().handle, &properties);
    uint32_t memoryType = UINT32_MAX;
    constexpr auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
    {
        if ((requirements.memoryTypeBits & (uint32_t{1} << index)) &&
            (properties.memoryTypes[index].propertyFlags & required) == required) { memoryType = index; break; }
    }
    if (memoryType == UINT32_MAX)
    {
        std::fprintf(stderr, "Vulkan error: lighting UBO requires HOST_VISIBLE | HOST_COHERENT memory.\n");
        return false;
    }
    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.buffer = created->buffer;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.pNext = &dedicated; allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = memoryType;
    if (!checkResult(allocateMemory(created->device, &allocation, nullptr, &created->memory), "vkAllocateMemory(lighting)") ||
        !checkResult(bindMemory(created->device, created->buffer, created->memory, 0), "vkBindBufferMemory(lighting)") ||
        !checkResult(mapMemory(created->device, created->memory, 0, sizeof(FrameLighting), 0, &created->mapped), "vkMapMemory(lighting)")) { return false; }

    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1; pool.poolSizeCount = 1; pool.pPoolSizes = &size;
    if (!checkResult(createPool(created->device, &pool, nullptr, &created->pool), "vkCreateDescriptorPool(lighting)")) { return false; }
    VkDescriptorSetAllocateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    sets.descriptorPool = created->pool; sets.descriptorSetCount = 1; sets.pSetLayouts = &layout;
    if (!checkResult(allocateSets(created->device, &sets, &created->descriptor), "vkAllocateDescriptorSets(lighting)")) { return false; }
    const VkDescriptorBufferInfo bufferInfo{created->buffer, 0, sizeof(FrameLighting)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = created->descriptor; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo = &bufferInfo;
    updateSets(created->device, 1, &write, 0, nullptr);
    state_ = std::move(created);
    std::fprintf(stderr, "Vulkan: Frame lighting buffer created; 32 bytes, coherent memory type %u.\n", memoryType);
    return true;
}
void VulkanFrameLighting::write(const FrameLighting& lighting) { std::memcpy(state_->mapped, &lighting, sizeof(lighting)); }
VkDescriptorSet VulkanFrameLighting::set() const { return state_ ? state_->descriptor : VK_NULL_HANDLE; }
VkBuffer VulkanFrameLighting::buffer() const { return state_ ? state_->buffer : VK_NULL_HANDLE; }
}
