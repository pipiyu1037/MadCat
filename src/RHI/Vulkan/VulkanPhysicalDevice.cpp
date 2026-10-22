#include "VulkanPhysicalDevice.h"

#include "VulkanFunctions.h"
#include "VulkanInstance.h"
#include "VulkanSurface.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace
{
using madcat::rhi::detail::checkResult;
using madcat::rhi::detail::enumerateProperties;
using madcat::rhi::detail::loadFunction;

bool supportsCoreVersion(uint32_t version, uint32_t required)
{
    return VK_API_VERSION_VARIANT(version) == 0 && version >= required;
}

struct RayQueryPrerequisites
{
    bool computeQueue;
    bool accelerationDependencies;
    bool rayDependencies;
};

RayQueryPrerequisites rayQueryPrerequisites(const madcat::rhi::PhysicalDeviceInfo& device)
{
    const bool core11 = supportsCoreVersion(device.properties.apiVersion, VK_API_VERSION_1_1);
    const bool core12 = supportsCoreVersion(device.properties.apiVersion, VK_API_VERSION_1_2);
    return {
        std::ranges::any_of(device.queueFamilies, [](const auto& queue) {
            return queue.queueCount != 0 && (queue.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        }),
        device.hasExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
            (core12 || (core11 && device.hasExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) &&
                device.hasExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))),
        core12 || (core11 && device.hasExtension(VK_KHR_SPIRV_1_4_EXTENSION_NAME) &&
            device.hasExtension(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME))
    };
}

const char* deviceTypeName(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    default: return "other";
    }
}

const char* supportName(bool supported)
{
    return supported ? "supported" : "unsupported";
}

const char* surfaceFormatName(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "A2B10G10R10_UNORM_PACK32";
    case VK_FORMAT_R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
    default: return "other";
    }
}

const char* presentModeName(VkPresentModeKHR mode)
{
    switch (mode)
    {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
    default: return "other";
    }
}

void printSurfaceCapabilities(const madcat::rhi::SurfaceSupport& support)
{
    if (!support.capabilities)
    {
        std::fprintf(stderr, "  Window Surface: unsupported; no presentation queue, skipping Surface capabilities.\n");
        return;
    }
    const auto& capabilities = *support.capabilities;
    std::fprintf(stderr, "  Surface image count: min=%u ", capabilities.minImageCount);
    if (capabilities.maxImageCount == 0)
    {
        std::fprintf(stderr, "max=unbounded (0; no Surface limit).\n");
    }
    else
    {
        std::fprintf(stderr, "max=%u.\n", capabilities.maxImageCount);
    }
    if (capabilities.currentExtent.width == UINT32_MAX)
    {
        std::fprintf(stderr, "  Surface current extent: application-selected.\n");
    }
    else
    {
        std::fprintf(stderr, "  Surface current extent: %ux%u.\n", capabilities.currentExtent.width, capabilities.currentExtent.height);
    }
    std::fprintf(stderr, "  Surface extent limits: min=%ux%u max=%ux%u; maxArrayLayers=%u.\n",
        capabilities.minImageExtent.width, capabilities.minImageExtent.height,
        capabilities.maxImageExtent.width, capabilities.maxImageExtent.height, capabilities.maxImageArrayLayers);
    std::fprintf(stderr, "  Surface flags: usage=0x%x transforms=0x%x currentTransform=0x%x compositeAlpha=0x%x.\n",
        capabilities.supportedUsageFlags, capabilities.supportedTransforms,
        capabilities.currentTransform, capabilities.supportedCompositeAlpha);
    std::fprintf(stderr, "  Surface formats: %zu.\n", support.formats.size());
    for (const auto& format : support.formats)
    {
        std::fprintf(stderr, "    %s (%d), colorSpace=%s (%d).\n", surfaceFormatName(format.format), static_cast<int>(format.format),
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ? "SRGB_NONLINEAR" : "other", static_cast<int>(format.colorSpace));
    }
    std::fprintf(stderr, "  Surface present modes: %zu.\n", support.presentModes.size());
    for (const auto mode : support.presentModes)
    {
        std::fprintf(stderr, "    %s (%d).\n", presentModeName(mode), static_cast<int>(mode));
    }
    if (support.formats.empty() || support.presentModes.empty())
    {
        std::fprintf(stderr, "  Surface has no usable format/present mode list; swapchain configuration unavailable.\n");
    }
}
}

namespace madcat::rhi
{
bool PhysicalDeviceInfo::hasExtension(const char* name) const
{
    return std::ranges::any_of(extensions, [&](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

bool PhysicalDeviceInfo::supportsComputeRayQuery() const
{
    const auto prerequisites = rayQueryPrerequisites(*this);
    return prerequisites.computeQueue && prerequisites.accelerationDependencies && prerequisites.rayDependencies &&
        features.bufferDeviceAddress.supported && features.accelerationStructure.supported && features.rayQuery.supported;
}

bool queryPhysicalDevices(const VulkanInstance& instance, std::vector<PhysicalDeviceInfo>& devices)
{
    if (instance.handle() == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: physical device queries require an initialized Instance.\n");
        return false;
    }
    const auto enumerateDevices = loadFunction<PFN_vkEnumeratePhysicalDevices>(instance.handle(), "vkEnumeratePhysicalDevices");
    const auto getProperties = loadFunction<PFN_vkGetPhysicalDeviceProperties2>(instance.handle(), "vkGetPhysicalDeviceProperties2");
    const auto getQueues = loadFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(instance.handle(), "vkGetPhysicalDeviceQueueFamilyProperties");
    const auto enumerateExtensions = loadFunction<PFN_vkEnumerateDeviceExtensionProperties>(instance.handle(), "vkEnumerateDeviceExtensionProperties");
    const auto getFeatures = loadFunction<PFN_vkGetPhysicalDeviceFeatures2>(instance.handle(), "vkGetPhysicalDeviceFeatures2");
    if (!enumerateDevices || !getProperties || !getQueues || !enumerateExtensions || !getFeatures)
    {
        return false;
    }
    std::vector<VkPhysicalDevice> handles;
    if (!enumerateProperties([&](uint32_t* count, VkPhysicalDevice* values) {
        return enumerateDevices(instance.handle(), count, values);
    }, "vkEnumeratePhysicalDevices", handles))
    {
        return false;
    }
    if (handles.empty())
    {
        std::fprintf(stderr, "Vulkan error: no physical devices were found. Check Vulkan drivers.\n");
        return false;
    }
    std::vector<PhysicalDeviceInfo> queried;
    queried.reserve(handles.size());
    for (size_t deviceIndex = 0; deviceIndex < handles.size(); ++deviceIndex)
    {
        PhysicalDeviceInfo device;
        device.instance = instance.handle();
        device.handle = handles[deviceIndex];
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        getProperties(device.handle, &properties);
        device.properties = properties.properties;
        if (!enumerateProperties([&](uint32_t* count, VkExtensionProperties* values) {
            return enumerateExtensions(device.handle, nullptr, count, values);
        }, "vkEnumerateDeviceExtensionProperties", device.extensions))
        {
            std::fprintf(stderr, "Vulkan error: extension query failed for device [%zu] %s.\n", deviceIndex, device.properties.deviceName);
            return false;
        }
        const bool core12 = supportsCoreVersion(device.properties.apiVersion, VK_API_VERSION_1_2);
        const bool core13 = supportsCoreVersion(device.properties.apiVersion, VK_API_VERSION_1_3);
        if (core12 || device.hasExtension(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))
        {
            VkPhysicalDeviceDriverProperties driver{};
            driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            properties.pNext = &driver;
            getProperties(device.handle, &properties);
            properties.pNext = nullptr;
            device.driver = driver;
        }
        uint32_t queueCount = 0;
        getQueues(device.handle, &queueCount, nullptr);
        device.queueFamilies.resize(queueCount);
        if (queueCount != 0)
        {
            getQueues(device.handle, &queueCount, device.queueFamilies.data());
            device.queueFamilies.resize(queueCount);
        }
        const bool canQueryDynamicRendering = core13 || device.hasExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        const bool canQuerySynchronization2 = core13 || device.hasExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        const bool canQueryAddress = core12 || device.hasExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        const bool canQueryAcceleration = device.hasExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        const bool canQueryRay = device.hasExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        const bool canQueryMaintenance = instance.surfaceMaintenanceEnabled() &&
            device.hasExtension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME);
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering{};
        dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
        VkPhysicalDeviceSynchronization2Features synchronization2{};
        synchronization2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
        VkPhysicalDeviceBufferDeviceAddressFeatures address{};
        address.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{};
        acceleration.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        VkPhysicalDeviceRayQueryFeaturesKHR ray{};
        ray.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{};
        maintenance.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT;
        void** nextFeature = &features.pNext;
        const auto appendFeature = [&](auto& feature) {
            *nextFeature = &feature;
            nextFeature = &feature.pNext;
        };
        if (canQueryDynamicRendering) { appendFeature(dynamicRendering); }
        if (canQuerySynchronization2) { appendFeature(synchronization2); }
        if (canQueryAddress) { appendFeature(address); }
        if (canQueryAcceleration) { appendFeature(acceleration); }
        if (canQueryRay) { appendFeature(ray); }
        if (canQueryMaintenance) { appendFeature(maintenance); }
        getFeatures(device.handle, &features);
        device.features = {
            {canQueryDynamicRendering, dynamicRendering.dynamicRendering == VK_TRUE},
            {canQuerySynchronization2, synchronization2.synchronization2 == VK_TRUE},
            {canQueryAddress, address.bufferDeviceAddress == VK_TRUE},
            {canQueryAcceleration, acceleration.accelerationStructure == VK_TRUE},
            {canQueryRay, ray.rayQuery == VK_TRUE},
            {canQueryMaintenance, maintenance.swapchainMaintenance1 == VK_TRUE}
        };
        if (canQueryAcceleration)
        {
            VkPhysicalDeviceAccelerationStructurePropertiesKHR limits{};
            limits.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
            properties.pNext = &limits;
            getProperties(device.handle, &properties);
            properties.pNext = nullptr;
            device.accelerationStructureLimits = limits;
        }
        queried.push_back(std::move(device));
    }
    devices = std::move(queried);
    return true;
}

bool querySurfaceSupport(const PhysicalDeviceInfo& device, const VulkanSurface& surface, SurfaceSupport& support)
{
    if (device.instance == VK_NULL_HANDLE || device.handle == VK_NULL_HANDLE || surface.handle() == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: Surface queries require a physical device and an initialized Surface.\n");
        return false;
    }
    if (device.instance != surface.instanceHandle())
    {
        std::fprintf(stderr, "Vulkan error: physical device and Surface must belong to the same Instance.\n");
        return false;
    }
    const auto getSupport = loadFunction<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(device.instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    const auto getCapabilities = loadFunction<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(device.instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    const auto getFormats = loadFunction<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(device.instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    const auto getModes = loadFunction<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(device.instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    if (!getSupport || !getCapabilities || !getFormats || !getModes)
    {
        return false;
    }
    SurfaceSupport queried;
    queried.physicalDevice = device.handle;
    queried.surface = surface.handle();
    queried.presentQueues.resize(device.queueFamilies.size(), VK_FALSE);
    bool hasPresentQueue = false;
    for (size_t queueIndex = 0; queueIndex < device.queueFamilies.size(); ++queueIndex)
    {
        if (!checkResult(getSupport(device.handle, static_cast<uint32_t>(queueIndex), surface.handle(), &queried.presentQueues[queueIndex]),
            "vkGetPhysicalDeviceSurfaceSupportKHR"))
        {
            std::fprintf(stderr, "Vulkan error: presentation query failed for device %s, queue [%zu].\n", device.properties.deviceName, queueIndex);
            return false;
        }
        hasPresentQueue |= queried.presentQueues[queueIndex] == VK_TRUE && device.queueFamilies[queueIndex].queueCount != 0;
    }
    // Capability queries are only valid for devices supporting this Surface.
    if (hasPresentQueue)
    {
        VkSurfaceCapabilitiesKHR capabilities{};
        if (!checkResult(getCapabilities(device.handle, surface.handle(), &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") ||
            !enumerateProperties([&](uint32_t* count, VkSurfaceFormatKHR* values) {
                return getFormats(device.handle, surface.handle(), count, values);
            }, "vkGetPhysicalDeviceSurfaceFormatsKHR", queried.formats) ||
            !enumerateProperties([&](uint32_t* count, VkPresentModeKHR* values) {
                return getModes(device.handle, surface.handle(), count, values);
            }, "vkGetPhysicalDeviceSurfacePresentModesKHR", queried.presentModes))
        {
            std::fprintf(stderr, "Vulkan error: Surface capability query failed for device %s.\n", device.properties.deviceName);
            return false;
        }
        queried.capabilities = capabilities;
    }
    support = std::move(queried);
    return true;
}

void printPhysicalDeviceReport(const PhysicalDeviceInfo& device, const SurfaceSupport& support, std::size_t deviceIndex)
{
    if (support.physicalDevice != device.handle || support.presentQueues.size() != device.queueFamilies.size())
    {
        std::fprintf(stderr, "Vulkan error: device report requires its matching Surface support snapshot.\n");
        return;
    }
    const auto& info = device.properties;
    std::fprintf(stderr, "Vulkan: device [%zu] %s (%s).\n", deviceIndex, info.deviceName, deviceTypeName(info.deviceType));
    std::fprintf(stderr, "  API %u.%u.%u (variant %u), vendor 0x%x, device 0x%x, driverVersion(raw) 0x%x.\n",
        VK_API_VERSION_MAJOR(info.apiVersion), VK_API_VERSION_MINOR(info.apiVersion), VK_API_VERSION_PATCH(info.apiVersion),
        VK_API_VERSION_VARIANT(info.apiVersion), info.vendorID, info.deviceID, info.driverVersion);
    if (device.driver)
    {
        std::fprintf(stderr, "  Driver: %s; %s (driverID %d).\n", device.driver->driverName, device.driver->driverInfo, static_cast<int>(device.driver->driverID));
    }
    std::fprintf(stderr, "  Queue families: %zu (presentation checked against window Surface).\n", device.queueFamilies.size());
    for (size_t queueIndex = 0; queueIndex < device.queueFamilies.size(); ++queueIndex)
    {
        const auto& queue = device.queueFamilies[queueIndex];
        const bool graphics = (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool compute = (queue.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        const bool transfer = graphics || compute || (queue.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
        std::fprintf(stderr, "    [%zu] count=%u flags=0x%x graphics=%s compute=%s transfer=%s present=%s.\n",
            queueIndex, queue.queueCount, queue.queueFlags, supportName(graphics), supportName(compute), supportName(transfer),
            supportName(support.presentQueues[queueIndex] == VK_TRUE));
    }
    const char* relevantExtensions[] = {
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
    };
    std::fprintf(stderr, "  Relevant extensions (promoted core support is checked separately):\n");
    for (const char* name : relevantExtensions)
    {
        std::fprintf(stderr, "    %s: %s.\n", name, supportName(device.hasExtension(name)));
    }
    printSurfaceCapabilities(support);
    const auto reportFeature = [](const char* name, const FeatureSupport& feature) {
        std::fprintf(stderr, "  Feature %s: %s.\n", name,
            feature.queryable ? supportName(feature.supported) : "unavailable (no core/extension provider)");
    };
    reportFeature("dynamicRendering", device.features.dynamicRendering);
    reportFeature("synchronization2", device.features.synchronization2);
    reportFeature("bufferDeviceAddress", device.features.bufferDeviceAddress);
    reportFeature("accelerationStructure", device.features.accelerationStructure);
    reportFeature("rayQuery", device.features.rayQuery);
    reportFeature("swapchainMaintenance1 (with instance dependencies)", device.features.swapchainMaintenance1);
    if (device.accelerationStructureLimits)
    {
        const auto& limits = *device.accelerationStructureLimits;
        std::fprintf(stderr, "  Acceleration structure limits: geometries=%llu instances=%llu primitives=%llu scratchAlignment=%u.\n",
            static_cast<unsigned long long>(limits.maxGeometryCount), static_cast<unsigned long long>(limits.maxInstanceCount),
            static_cast<unsigned long long>(limits.maxPrimitiveCount), limits.minAccelerationStructureScratchOffsetAlignment);
    }
    const bool rayAvailable = device.supportsComputeRayQuery();
    std::fprintf(stderr, "  Compute Ray Query prerequisites: %s (query only; Shader/GPU probe pending).\n", rayAvailable ? "available" : "missing");
    if (!rayAvailable)
    {
        const auto prerequisites = rayQueryPrerequisites(device);
        if (!prerequisites.computeQueue) { std::fprintf(stderr, "    Missing compute queue.\n"); }
        if (!prerequisites.accelerationDependencies) { std::fprintf(stderr, "    Missing acceleration structure extension dependencies.\n"); }
        if (!prerequisites.rayDependencies) { std::fprintf(stderr, "    Missing SPIR-V 1.4 / shader float controls dependencies.\n"); }
        if (!device.features.bufferDeviceAddress.supported) { std::fprintf(stderr, "    Missing bufferDeviceAddress support.\n"); }
        if (!device.features.accelerationStructure.supported) { std::fprintf(stderr, "    Missing accelerationStructure support.\n"); }
        if (!device.features.rayQuery.supported) { std::fprintf(stderr, "    Missing rayQuery support.\n"); }
    }
}
}
