#include "VulkanInstance.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
constexpr uint32_t requiredApiVersion = VK_API_VERSION_1_3;
constexpr const char* validationLayer = "VK_LAYER_KHRONOS_validation";

#ifdef NDEBUG
constexpr bool enableValidation = false;
#else
constexpr bool enableValidation = true;
#endif

bool checkResult(VkResult result, const char* operation)
{
    if (result == VK_SUCCESS)
    {
        return true;
    }
    std::fprintf(stderr, "Vulkan error: %s failed (VkResult %d).\n",
        operation, static_cast<int>(result));
    return false;
}

template<typename Function>
Function loadFunction(VkInstance instance, const char* name)
{
    const auto function = reinterpret_cast<Function>(glfwGetInstanceProcAddress(instance, name));
    if (!function)
    {
        std::fprintf(stderr, "Vulkan error: required function %s is unavailable.\n", name);
    }
    return function;
}

template<typename Property, typename Enumerate>
bool enumerateProperties(Enumerate enumerate, const char* operation, std::vector<Property>& properties)
{
    VkResult result;
    do
    {
        uint32_t count = 0;
        if (!checkResult(enumerate(&count, nullptr), operation))
        {
            return false;
        }
        properties.resize(count);
        if (count == 0)
        {
            return true;
        }
        result = enumerate(&count, properties.data());
        if (result != VK_SUCCESS && result != VK_INCOMPLETE)
        {
            return checkResult(result, operation);
        }
        properties.resize(count);
    } while (result == VK_INCOMPLETE);
    return checkResult(result, operation);
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

bool supportsCoreVersion(uint32_t version, uint32_t required)
{
    return VK_API_VERSION_VARIANT(version) == 0 && version >= required;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    const char* level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error" : "warning";
    std::fprintf(stderr, "Vulkan [%s] [types=0x%x] [%s]: %s\n", level, types,
        data->pMessageIdName ? data->pMessageIdName : "unnamed",
        data->pMessage ? data->pMessage : "(no message)");
    return VK_FALSE;
}
}

namespace madcat::rhi
{
VulkanInstance::~VulkanInstance()
{
    if (debugMessenger_ != VK_NULL_HANDLE)
    {
        destroyDebugMessenger_(instance_, debugMessenger_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE)
    {
        destroyInstance_(instance_, nullptr);
        std::fprintf(stderr, "Vulkan: Instance destroyed.\n");
    }
}

bool VulkanInstance::initialize()
{
    if (instance_ != VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: this Instance owner is already initialized.\n");
        return false;
    }
    if (glfwVulkanSupported() != GLFW_TRUE)
    {
        std::fprintf(stderr, "Vulkan error: no usable Vulkan loader/driver was found. Install a Vulkan-capable graphics driver.\n");
        return false;
    }

    uint32_t supportedVersion = VK_API_VERSION_1_0;
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        glfwGetInstanceProcAddress(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerateVersion && !checkResult(enumerateVersion(&supportedVersion), "vkEnumerateInstanceVersion"))
    {
        return false;
    }
    std::fprintf(stderr, "Vulkan: instance-level API support %u.%u.%u; requesting 1.3.0.\n",
        VK_API_VERSION_MAJOR(supportedVersion), VK_API_VERSION_MINOR(supportedVersion),
        VK_API_VERSION_PATCH(supportedVersion));
    if (VK_API_VERSION_VARIANT(supportedVersion) != 0 || supportedVersion < requiredApiVersion)
    {
        std::fprintf(stderr, "Vulkan error: MadCat requires instance-level Vulkan 1.3 or newer.\n");
        return false;
    }

    const auto enumerateExtensions = loadFunction<PFN_vkEnumerateInstanceExtensionProperties>(
        VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    const auto createInstance = loadFunction<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance");
    // GLFW falls back to loader exports. Resolve destruction before acquiring ownership.
    destroyInstance_ = loadFunction<PFN_vkDestroyInstance>(VK_NULL_HANDLE, "vkDestroyInstance");
    if (!enumerateExtensions || !createInstance || !destroyInstance_)
    {
        return false;
    }

    std::vector<VkExtensionProperties> availableExtensions;
    if (!enumerateProperties([&](uint32_t* count, VkExtensionProperties* properties) {
        return enumerateExtensions(nullptr, count, properties);
    }, "vkEnumerateInstanceExtensionProperties", availableExtensions))
    {
        return false;
    }

    if constexpr (enableValidation)
    {
        const auto enumerateLayers = loadFunction<PFN_vkEnumerateInstanceLayerProperties>(
            VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties");
        std::vector<VkLayerProperties> layers;
        if (!enumerateLayers || !enumerateProperties(enumerateLayers, "vkEnumerateInstanceLayerProperties", layers))
        {
            return false;
        }
        if (!std::ranges::any_of(layers, [](const auto& layer) {
            return std::strcmp(layer.layerName, validationLayer) == 0;
        }))
        {
            std::fprintf(stderr, "Vulkan error: Debug build requires %s. Install the Vulkan SDK validation layers and check layer search paths.\n",
                validationLayer);
            return false;
        }
        // An enabled layer can also provide instance extensions.
        std::vector<VkExtensionProperties> layerExtensions;
        if (!enumerateProperties([&](uint32_t* count, VkExtensionProperties* properties) {
            return enumerateExtensions(validationLayer, count, properties);
        }, "vkEnumerateInstanceExtensionProperties(validation)", layerExtensions))
        {
            return false;
        }
        availableExtensions.insert(availableExtensions.end(), layerExtensions.begin(), layerExtensions.end());
    }

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions || glfwExtensionCount == 0)
    {
        std::fprintf(stderr, "Vulkan error: GLFW could not find the required window surface extensions. Check graphics driver installation and Vulkan driver search paths.\n");
        return false;
    }
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if constexpr (enableValidation)
    {
        if (!std::ranges::any_of(extensions, [](const char* name) {
            return std::strcmp(name, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
        }))
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }
    for (const char* name : extensions)
    {
        if (!std::ranges::any_of(availableExtensions, [name](const auto& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        }))
        {
            std::fprintf(stderr, "Vulkan error: required instance extension %s is unavailable.\n", name);
            return false;
        }
        std::fprintf(stderr, "Vulkan: enabling instance extension %s.\n", name);
    }

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
    debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = debugCallback;

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "MadCat";
    applicationInfo.apiVersion = requiredApiVersion;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    if constexpr (enableValidation)
    {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &validationLayer;
        // This temporary callback covers vkCreateInstance and vkDestroyInstance.
        createInfo.pNext = &debugInfo;
    }

    VkInstance createdInstance = VK_NULL_HANDLE;
    if (!checkResult(createInstance(&createInfo, nullptr, &createdInstance), "vkCreateInstance"))
    {
        return false;
    }
    instance_ = createdInstance;
    std::fprintf(stderr, "Vulkan: Instance created.\n");

    if constexpr (enableValidation)
    {
        const auto createDebugMessenger = loadFunction<PFN_vkCreateDebugUtilsMessengerEXT>(
            instance_, "vkCreateDebugUtilsMessengerEXT");
        destroyDebugMessenger_ = loadFunction<PFN_vkDestroyDebugUtilsMessengerEXT>(
            instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (!createDebugMessenger || !destroyDebugMessenger_)
        {
            return false;
        }
        VkDebugUtilsMessengerEXT createdMessenger = VK_NULL_HANDLE;
        if (!checkResult(createDebugMessenger(instance_, &debugInfo, nullptr, &createdMessenger),
            "vkCreateDebugUtilsMessengerEXT"))
        {
            return false;
        }
        debugMessenger_ = createdMessenger;
        std::fprintf(stderr, "Vulkan: validation enabled (%s); diagnostic callback ready.\n", validationLayer);
    }
    else
    {
        std::fprintf(stderr, "Vulkan: validation disabled (NDEBUG build).\n");
    }
    return true;
}

bool VulkanInstance::reportPhysicalDevices() const
{
    if (instance_ == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: physical device queries require an initialized Instance.\n");
        return false;
    }

    const auto enumerateDevices = loadFunction<PFN_vkEnumeratePhysicalDevices>(
        instance_, "vkEnumeratePhysicalDevices");
    const auto getProperties = loadFunction<PFN_vkGetPhysicalDeviceProperties2>(
        instance_, "vkGetPhysicalDeviceProperties2");
    const auto getQueues = loadFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        instance_, "vkGetPhysicalDeviceQueueFamilyProperties");
    const auto enumerateExtensions = loadFunction<PFN_vkEnumerateDeviceExtensionProperties>(
        instance_, "vkEnumerateDeviceExtensionProperties");
    const auto getFeatures = loadFunction<PFN_vkGetPhysicalDeviceFeatures2>(
        instance_, "vkGetPhysicalDeviceFeatures2");
    if (!enumerateDevices || !getProperties || !getQueues || !enumerateExtensions || !getFeatures)
    {
        return false;
    }

    std::vector<VkPhysicalDevice> devices;
    if (!enumerateProperties([&](uint32_t* count, VkPhysicalDevice* values) {
        return enumerateDevices(instance_, count, values);
    }, "vkEnumeratePhysicalDevices", devices))
    {
        return false;
    }
    if (devices.empty())
    {
        std::fprintf(stderr, "Vulkan error: no physical devices were found. Check Vulkan drivers.\n");
        return false;
    }
    std::fprintf(stderr, "Vulkan: found %zu physical device(s).\n", devices.size());

    for (size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex)
    {
        const VkPhysicalDevice device = devices[deviceIndex];
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        getProperties(device, &properties);
        const auto& info = properties.properties;
        std::fprintf(stderr, "Vulkan: device [%zu] %s (%s).\n", deviceIndex,
            info.deviceName, deviceTypeName(info.deviceType));
        std::fprintf(stderr, "  API %u.%u.%u (variant %u), vendor 0x%x, device 0x%x, driverVersion(raw) 0x%x.\n",
            VK_API_VERSION_MAJOR(info.apiVersion), VK_API_VERSION_MINOR(info.apiVersion),
            VK_API_VERSION_PATCH(info.apiVersion), VK_API_VERSION_VARIANT(info.apiVersion),
            info.vendorID, info.deviceID, info.driverVersion);

        std::vector<VkExtensionProperties> extensions;
        if (!enumerateProperties([&](uint32_t* count, VkExtensionProperties* values) {
            return enumerateExtensions(device, nullptr, count, values);
        }, "vkEnumerateDeviceExtensionProperties", extensions))
        {
            std::fprintf(stderr, "Vulkan error: extension query failed for device [%zu] %s.\n",
                deviceIndex, info.deviceName);
            return false;
        }
        const auto hasExtension = [&](const char* name) {
            return std::ranges::any_of(extensions, [&](const auto& extension) {
                return std::strcmp(extension.extensionName, name) == 0;
            });
        };
        const bool core11 = supportsCoreVersion(info.apiVersion, VK_API_VERSION_1_1);
        const bool core12 = supportsCoreVersion(info.apiVersion, VK_API_VERSION_1_2);
        const bool core13 = supportsCoreVersion(info.apiVersion, VK_API_VERSION_1_3);

        if (core12 || hasExtension(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))
        {
            VkPhysicalDeviceDriverProperties driver{};
            driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            properties.pNext = &driver;
            getProperties(device, &properties);
            properties.pNext = nullptr;
            std::fprintf(stderr, "  Driver: %s; %s (driverID %d).\n",
                driver.driverName, driver.driverInfo, static_cast<int>(driver.driverID));
        }

        uint32_t queueCount = 0;
        getQueues(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        if (queueCount != 0)
        {
            getQueues(device, &queueCount, queues.data());
            queues.resize(queueCount);
        }
        bool hasComputeQueue = false;
        std::fprintf(stderr, "  Queue families: %zu (surface presentation not checked).\n", queues.size());
        for (size_t queueIndex = 0; queueIndex < queues.size(); ++queueIndex)
        {
            const auto& queue = queues[queueIndex];
            const bool graphics = (queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            const bool compute = (queue.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
            // Graphics and compute queues also support transfer commands, even without the transfer bit.
            const bool transfer = graphics || compute || (queue.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
            hasComputeQueue |= compute && queue.queueCount != 0;
            std::fprintf(stderr, "    [%zu] count=%u flags=0x%x graphics=%s compute=%s transfer=%s.\n",
                queueIndex, queue.queueCount, queue.queueFlags,
                supportName(graphics), supportName(compute), supportName(transfer));
        }

        const char* relevantExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_SPIRV_1_4_EXTENSION_NAME,
            VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
        };
        std::fprintf(stderr, "  Relevant extensions (promoted core support is checked separately):\n");
        for (const char* name : relevantExtensions)
        {
            std::fprintf(stderr, "    %s: %s.\n", name, supportName(hasExtension(name)));
        }

        const bool canQueryDynamicRendering = core13 || hasExtension(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        const bool canQuerySynchronization2 = core13 || hasExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        const bool canQueryAddress = core12 || hasExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        const bool canQueryAcceleration = hasExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        const bool canQueryRay = hasExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);

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

        // Only append structures provided by this device's core version or advertised extensions.
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
        getFeatures(device, &features);

        const auto reportFeature = [](const char* name, bool queryable, VkBool32 supported) {
            std::fprintf(stderr, "  Feature %s: %s.\n", name,
                queryable ? supportName(supported == VK_TRUE) : "unavailable (no core/extension provider)");
        };
        reportFeature("dynamicRendering", canQueryDynamicRendering, dynamicRendering.dynamicRendering);
        reportFeature("synchronization2", canQuerySynchronization2, synchronization2.synchronization2);
        reportFeature("bufferDeviceAddress", canQueryAddress, address.bufferDeviceAddress);
        reportFeature("accelerationStructure", canQueryAcceleration, acceleration.accelerationStructure);
        reportFeature("rayQuery", canQueryRay, ray.rayQuery);

        if (canQueryAcceleration)
        {
            VkPhysicalDeviceAccelerationStructurePropertiesKHR limits{};
            limits.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
            properties.pNext = &limits;
            getProperties(device, &properties);
            properties.pNext = nullptr;
            std::fprintf(stderr, "  Acceleration structure limits: geometries=%llu instances=%llu primitives=%llu scratchAlignment=%u.\n",
                static_cast<unsigned long long>(limits.maxGeometryCount),
                static_cast<unsigned long long>(limits.maxInstanceCount),
                static_cast<unsigned long long>(limits.maxPrimitiveCount),
                limits.minAccelerationStructureScratchOffsetAlignment);
        }

        const bool accelerationDependencies = hasExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
            (core12 || (core11 && hasExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) &&
                hasExtension(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME)));
        const bool rayDependencies = core12 ||
            (core11 && hasExtension(VK_KHR_SPIRV_1_4_EXTENSION_NAME) &&
                hasExtension(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME));
        const bool rayPrerequisites = hasComputeQueue && accelerationDependencies && rayDependencies &&
            canQueryAddress && address.bufferDeviceAddress == VK_TRUE &&
            canQueryAcceleration && acceleration.accelerationStructure == VK_TRUE &&
            canQueryRay && ray.rayQuery == VK_TRUE;
        std::fprintf(stderr, "  Compute Ray Query prerequisites: %s (query only; Shader/GPU probe pending).\n",
            rayPrerequisites ? "available" : "missing");
        if (!rayPrerequisites)
        {
            if (!hasComputeQueue) { std::fprintf(stderr, "    Missing compute queue.\n"); }
            if (!accelerationDependencies) { std::fprintf(stderr, "    Missing acceleration structure extension dependencies.\n"); }
            if (!rayDependencies) { std::fprintf(stderr, "    Missing SPIR-V 1.4 / shader float controls dependencies.\n"); }
            if (!canQueryAddress || address.bufferDeviceAddress != VK_TRUE) { std::fprintf(stderr, "    Missing bufferDeviceAddress support.\n"); }
            if (!canQueryAcceleration || acceleration.accelerationStructure != VK_TRUE) { std::fprintf(stderr, "    Missing accelerationStructure support.\n"); }
            if (!canQueryRay || ray.rayQuery != VK_TRUE) { std::fprintf(stderr, "    Missing rayQuery support.\n"); }
        }
    }
    std::fprintf(stderr, "Vulkan: physical device capability report complete; no device selected or features enabled.\n");
    return true;
}
}
