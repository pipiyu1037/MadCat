#include "VulkanSwapchain.h"

#include "VulkanDevice.h"
#include "VulkanFunctions.h"
#include "VulkanSurface.h"

#include <algorithm>
#include <utility>

namespace
{
bool chooseSwapchainConfig(const madcat::rhi::SurfaceSupport& support, VkExtent2D framebufferExtent, VkSwapchainCreateInfoKHR& info)
{
    if (!support.capabilities || support.formats.empty() ||
        std::ranges::find(support.presentModes, VK_PRESENT_MODE_FIFO_KHR) == support.presentModes.end())
    {
        std::fprintf(stderr, "Vulkan error: Swapchain requires Surface capabilities, formats and FIFO presentation.\n");
        return false;
    }
    const auto& capabilities = *support.capabilities;
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0 || capabilities.maxImageArrayLayers < 1 ||
        capabilities.minImageCount == 0 || (capabilities.maxImageCount != 0 && capabilities.maxImageCount < capabilities.minImageCount) ||
        capabilities.minImageExtent.width > capabilities.maxImageExtent.width ||
        capabilities.minImageExtent.height > capabilities.maxImageExtent.height)
    {
        std::fprintf(stderr, "Vulkan error: Surface cannot provide the required Swapchain image configuration.\n");
        return false;
    }

    // Prefer an sRGB attachment; otherwise retain a returned format/color-space
    // pair. Later rendering must account for the actual selected format.
    auto format = std::ranges::find_if(support.formats, [](const auto& entry) {
        return entry.format == VK_FORMAT_B8G8R8A8_SRGB && entry.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (format == support.formats.end())
    {
        format = std::ranges::find_if(support.formats, [](const auto& entry) { return entry.format != VK_FORMAT_UNDEFINED; });
    }
    if (format == support.formats.end())
    {
        std::fprintf(stderr, "Vulkan error: Surface has no concrete Swapchain format.\n");
        return false;
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX && extent.height == UINT32_MAX)
    {
        extent.width = std::clamp(framebufferExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(framebufferExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0 || extent.width == 0 || extent.height == 0 ||
        extent.width < capabilities.minImageExtent.width || extent.width > capabilities.maxImageExtent.width ||
        extent.height < capabilities.minImageExtent.height || extent.height > capabilities.maxImageExtent.height)
    {
        std::fprintf(stderr, "Vulkan error: Swapchain requires a non-zero supported pixel extent; defer creation while minimized.\n");
        return false;
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha{};
    for (const auto mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
    {
        if ((capabilities.supportedCompositeAlpha & mode) != 0)
        {
            compositeAlpha = mode;
            break;
        }
    }
    if (compositeAlpha == 0 || (capabilities.supportedTransforms & capabilities.currentTransform) == 0)
    {
        std::fprintf(stderr, "Vulkan error: Surface has no supported composite alpha or current transform for Swapchain creation.\n");
        return false;
    }
    uint32_t imageCount = capabilities.minImageCount;
    if (imageCount < UINT32_MAX) { ++imageCount; }
    if (capabilities.maxImageCount != 0) { imageCount = std::min(imageCount, capabilities.maxImageCount); }

    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = support.surface;
    info.minImageCount = imageCount;
    info.imageFormat = format->format;
    info.imageColorSpace = format->colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = compositeAlpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    return true;
}
}

namespace madcat::rhi
{
VulkanSwapchain::~VulkanSwapchain()
{
    if (swapchain_ != VK_NULL_HANDLE)
    {
        destroySwapchain_(device_, swapchain_, nullptr);
        std::fprintf(stderr, "Vulkan: Swapchain destroyed.\n");
    }
}

bool VulkanSwapchain::initialize(const VulkanDevice& device, const VulkanSurface& surface, VkExtent2D framebufferExtent)
{
    if (swapchain_ != VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: this Swapchain owner is already initialized.\n");
        return false;
    }
    const auto& physical = device.physicalDeviceInfo();
    if (device.handle() == VK_NULL_HANDLE || surface.handle() == VK_NULL_HANDLE || physical.instance != surface.instanceHandle())
    {
        std::fprintf(stderr, "Vulkan error: Swapchain requires an initialized Device and Surface from the same Instance.\n");
        return false;
    }
    SurfaceSupport support;
    if (!querySurfaceSupport(physical, surface, support)) { return false; }
    if (device.queueFamilyIndex() >= support.presentQueues.size() || support.presentQueues[device.queueFamilyIndex()] != VK_TRUE)
    {
        std::fprintf(stderr, "Vulkan error: Device queue family %u cannot present to the refreshed Surface.\n", device.queueFamilyIndex());
        return false;
    }
    VkSwapchainCreateInfoKHR info{};
    if (!chooseSwapchainConfig(support, framebufferExtent, info)) { return false; }

    const auto getDeviceProcAddress = detail::loadFunction<PFN_vkGetDeviceProcAddr>(physical.instance, "vkGetDeviceProcAddr");
    if (!getDeviceProcAddress) { return false; }
    const auto createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(getDeviceProcAddress(device.handle(), "vkCreateSwapchainKHR"));
    const auto destroySwapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(getDeviceProcAddress(device.handle(), "vkDestroySwapchainKHR"));
    const auto getImages = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(getDeviceProcAddress(device.handle(), "vkGetSwapchainImagesKHR"));
    if (!createSwapchain || !destroySwapchain || !getImages)
    {
        std::fprintf(stderr, "Vulkan error: required device function %s is unavailable.\n",
            !createSwapchain ? "vkCreateSwapchainKHR" : !destroySwapchain ? "vkDestroySwapchainKHR" : "vkGetSwapchainImagesKHR");
        return false;
    }

    // Keep partial creation under a local owner, including allocation exceptions
    // while enumerating images. Commit only after all initialization succeeds.
    VulkanSwapchain created;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    if (!detail::checkResult(createSwapchain(device.handle(), &info, nullptr, &handle), "vkCreateSwapchainKHR")) { return false; }
    created.device_ = device.handle();
    created.swapchain_ = handle;
    created.destroySwapchain_ = destroySwapchain;
    if (!detail::enumerateProperties([&](uint32_t* count, VkImage* values) {
        return getImages(device.handle(), handle, count, values);
    }, "vkGetSwapchainImagesKHR", created.images_)) { return false; }
    if (created.images_.size() < info.minImageCount || std::ranges::find(created.images_, VK_NULL_HANDLE) != created.images_.end())
    {
        std::fprintf(stderr, "Vulkan error: Swapchain returned too few images or a null image handle.\n");
        return false;
    }

    device_ = created.device_;
    swapchain_ = std::exchange(created.swapchain_, VK_NULL_HANDLE);
    surfaceFormat_ = {info.imageFormat, info.imageColorSpace};
    extent_ = info.imageExtent;
    images_ = std::move(created.images_);
    destroySwapchain_ = destroySwapchain;
    std::fprintf(stderr, "Vulkan: Swapchain created; %u x %u, format %d, color space %d, FIFO, %zu images (requested %u).\n",
        extent_.width, extent_.height, static_cast<int>(surfaceFormat_.format), static_cast<int>(surfaceFormat_.colorSpace), images_.size(), info.minImageCount);
    std::fprintf(stderr, "Vulkan: Swapchain usage 0x%x, transform 0x%x, composite alpha 0x%x; exclusive sharing.\n",
        info.imageUsage, info.preTransform, info.compositeAlpha);
    return true;
}
}
