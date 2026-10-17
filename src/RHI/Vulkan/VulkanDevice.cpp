#include "VulkanDevice.h"

#include "VulkanFunctions.h"

#include <optional>

namespace
{
int devicePreference(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 2;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 1;
    default: return 0;
    }
}

std::optional<uint32_t> suitableQueue(const madcat::rhi::PhysicalDeviceCandidate& candidate, size_t index)
{
    const auto& device = candidate.device;
    const auto& support = candidate.surfaceSupport;
    bool suitable = true;
    const auto require = [&](bool condition, const char* reason) {
        if (!condition)
        {
            std::fprintf(stderr, "Vulkan: device [%zu] %s rejected: %s.\n", index, device.properties.deviceName, reason);
            suitable = false;
        }
    };
    require(VK_API_VERSION_VARIANT(device.properties.apiVersion) == 0 && device.properties.apiVersion >= VK_API_VERSION_1_3,
        "device API must support Vulkan 1.3");
    require(device.hasExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME), "missing VK_KHR_swapchain");
    require(device.features.dynamicRendering.queryable && device.features.dynamicRendering.supported, "missing dynamicRendering");
    require(device.features.synchronization2.queryable && device.features.synchronization2.supported, "missing synchronization2");
    require(support.capabilities.has_value(), "no Surface capabilities");
    require(!support.formats.empty(), "no Surface formats");
    require(!support.presentModes.empty(), "no Surface present modes");
    if (support.capabilities)
    {
        require((support.capabilities->supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0,
            "Surface does not support color attachments");
    }
    std::optional<uint32_t> queueFamily;
    constexpr auto requiredFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    for (size_t queueIndex = 0; queueIndex < device.queueFamilies.size(); ++queueIndex)
    {
        const auto& queue = device.queueFamilies[queueIndex];
        if (queue.queueCount != 0 && (queue.queueFlags & requiredFlags) == requiredFlags && support.presentQueues[queueIndex] == VK_TRUE)
        {
            queueFamily = static_cast<uint32_t>(queueIndex);
            break;
        }
    }
    require(queueFamily.has_value(), "no single Graphics/Compute/Present queue family");
    return suitable ? queueFamily : std::nullopt;
}
}

namespace madcat::rhi
{
VulkanDevice::~VulkanDevice()
{
    if (device_ != VK_NULL_HANDLE)
    {
        // This is a final Device teardown guard, not a substitute for waiting
        // before releasing individual GPU resources or reusing frame storage.
        detail::checkResult(waitIdle_(device_), "vkDeviceWaitIdle");
        destroyDevice_(device_, nullptr);
        std::fprintf(stderr, "Vulkan: Device destroyed.\n");
    }
}

bool VulkanDevice::initialize(std::span<const PhysicalDeviceCandidate> candidates)
{
    if (device_ != VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: this Device owner is already initialized.\n");
        return false;
    }
    if (candidates.empty())
    {
        std::fprintf(stderr, "Vulkan error: Device selection requires physical device candidates.\n");
        return false;
    }
    // Validate snapshot identity before indexing queue data or calling Vulkan.
    const auto instance = candidates.front().device.instance;
    const auto surface = candidates.front().surfaceSupport.surface;
    for (size_t index = 0; index < candidates.size(); ++index)
    {
        const auto& candidate = candidates[index];
        if (instance == VK_NULL_HANDLE || surface == VK_NULL_HANDLE || candidate.device.instance != instance ||
            candidate.device.handle == VK_NULL_HANDLE || candidate.surfaceSupport.surface != surface ||
            candidate.surfaceSupport.physicalDevice != candidate.device.handle ||
            candidate.surfaceSupport.presentQueues.size() != candidate.device.queueFamilies.size())
        {
            std::fprintf(stderr, "Vulkan error: device candidate [%zu] requires matching device/Surface snapshots from the same Instance and Surface.\n", index);
            return false;
        }
    }
    const PhysicalDeviceInfo* selected = nullptr;
    uint32_t queueFamilyIndex = UINT32_MAX;
    for (size_t index = 0; index < candidates.size(); ++index)
    {
        if (const auto queueFamily = suitableQueue(candidates[index], index))
        {
            const auto& device = candidates[index].device;
            std::fprintf(stderr, "Vulkan: device [%zu] %s eligible, queue family %u, preference %d.\n",
                index, device.properties.deviceName, *queueFamily, devicePreference(device.properties.deviceType));
            // Equal preferences retain the first device in enumeration order.
            if (!selected || devicePreference(device.properties.deviceType) > devicePreference(selected->properties.deviceType))
            {
                selected = &device;
                queueFamilyIndex = *queueFamily;
            }
        }
    }
    if (!selected)
    {
        std::fprintf(stderr, "Vulkan error: no suitable physical device for Vulkan 1.3 and a single Graphics/Compute/Present queue.\n");
        return false;
    }
    std::fprintf(stderr, "Vulkan: selected %s, queue family %u (preference: discrete > integrated > other; ties use enumeration order).\n",
        selected->properties.deviceName, queueFamilyIndex);

    const auto createDevice = detail::loadFunction<PFN_vkCreateDevice>(instance, "vkCreateDevice");
    const auto getDeviceProcAddress = detail::loadFunction<PFN_vkGetDeviceProcAddr>(instance, "vkGetDeviceProcAddr");
    // Core device commands may be resolved through the Instance. Acquire a
    // destroy function first so failed device-command loading cannot leak.
    const auto destroyDevice = detail::loadFunction<PFN_vkDestroyDevice>(instance, "vkDestroyDevice");
    if (!createDevice || !getDeviceProcAddress || !destroyDevice)
    {
        return false;
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceVulkan13Features features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = extensions;

    VkDevice createdDevice = VK_NULL_HANDLE;
    if (!detail::checkResult(createDevice(selected->handle, &createInfo, nullptr, &createdDevice), "vkCreateDevice"))
    {
        std::fprintf(stderr, "Vulkan error: Device creation failed for %s, queue family %u.\n", selected->properties.deviceName, queueFamilyIndex);
        return false;
    }
    const auto getQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(getDeviceProcAddress(createdDevice, "vkGetDeviceQueue"));
    const auto waitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(getDeviceProcAddress(createdDevice, "vkDeviceWaitIdle"));
    if (!getQueue || !waitIdle)
    {
        std::fprintf(stderr, "Vulkan error: required device function %s is unavailable.\n", !getQueue ? "vkGetDeviceQueue" : "vkDeviceWaitIdle");
        destroyDevice(createdDevice, nullptr);
        return false;
    }
    VkQueue queue = VK_NULL_HANDLE;
    getQueue(createdDevice, queueFamilyIndex, 0, &queue);
    if (queue == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: vkGetDeviceQueue returned a null queue for %s, family %u.\n", selected->properties.deviceName, queueFamilyIndex);
        destroyDevice(createdDevice, nullptr);
        return false;
    }
    device_ = createdDevice;
    physicalDevice_ = selected->handle;
    queue_ = queue;
    queueFamilyIndex_ = queueFamilyIndex;
    destroyDevice_ = destroyDevice;
    waitIdle_ = waitIdle;
    std::fprintf(stderr, "Vulkan: Device created; queue family %u, queue 0 (Graphics/Compute/Present).\n", queueFamilyIndex_);
    std::fprintf(stderr, "Vulkan: enabled VK_KHR_swapchain, dynamicRendering and synchronization2; ray tracing features remain disabled.\n");
    return true;
}
}
