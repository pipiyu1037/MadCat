#pragma once

#include <vulkan/vulkan.h>
#include <cstdio>

namespace madcat::rhi::detail
{
struct TextureState
{
    VkDevice device = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0, mipLevels = 1;
    bool sampled = false;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkDestroyImageView destroyView = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    ~TextureState()
    {
        if (view) { destroyView(device, view, nullptr); }
        if (image) { destroyImage(device, image, nullptr); }
        if (memory) { freeMemory(device, memory, nullptr); }
        if (image) { std::fprintf(stderr, "Vulkan: %s texture destroyed.\n", sampled ? "Sampled" : "Depth"); }
    }
};
}
