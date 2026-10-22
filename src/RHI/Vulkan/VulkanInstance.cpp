#include "VulkanInstance.h"

#include "VulkanFunctions.h"

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

using madcat::rhi::detail::checkResult;
using madcat::rhi::detail::enumerateProperties;
using madcat::rhi::detail::loadFunction;

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
    const auto hasExtension = [&](const char* name) {
        return std::ranges::any_of(availableExtensions, [name](const auto& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
    };
    // Optional instance dependencies for present fences. Basic initialization
    // remains usable without them; the clear renderer requires this capability.
    const bool surfaceMaintenance = hasExtension(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME) &&
        hasExtension(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    if (surfaceMaintenance)
    {
        extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        extensions.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    }
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
    surfaceMaintenanceEnabled_ = surfaceMaintenance;
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

}
