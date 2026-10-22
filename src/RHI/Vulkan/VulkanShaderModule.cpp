#include "VulkanShaderModule.h"

#include "VulkanDevice.h"
#include "VulkanFunctions.h"
#include "Shader/Spirv.h"

namespace madcat::rhi
{
VulkanShaderModule::~VulkanShaderModule()
{
    if (module_ != VK_NULL_HANDLE)
    {
        destroyModule_(device_, module_, nullptr);
        std::fprintf(stderr, "Vulkan: Shader Module destroyed.\n");
    }
}

bool VulkanShaderModule::initialize(const VulkanDevice& device, const std::filesystem::path& file)
{
    if (module_ != VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: this Shader Module owner is already initialized.\n");
        return false;
    }
    if (device.handle() == VK_NULL_HANDLE)
    {
        std::fprintf(stderr, "Vulkan error: Shader Module creation requires an initialized Device.\n");
        return false;
    }
    const auto fileName = file.u8string();
    std::fprintf(stderr, "Vulkan: loading Shader Module from %s.\n", reinterpret_cast<const char*>(fileName.c_str()));
    std::vector<uint32_t> words;
    if (!shader::loadSpirv(file, words)) { return false; }
    const auto getProc = detail::loadFunction<PFN_vkGetDeviceProcAddr>(device.physicalDeviceInfo().instance, "vkGetDeviceProcAddr");
    if (!getProc) { return false; }
    // Resolve destruction before creation, so every acquired module has cleanup.
    const auto destroyModule = reinterpret_cast<PFN_vkDestroyShaderModule>(getProc(device.handle(), "vkDestroyShaderModule"));
    const auto createModule = reinterpret_cast<PFN_vkCreateShaderModule>(getProc(device.handle(), "vkCreateShaderModule"));
    if (!destroyModule || !createModule)
    {
        std::fprintf(stderr, "Vulkan error: required device function %s is unavailable.\n",
            !destroyModule ? "vkDestroyShaderModule" : "vkCreateShaderModule");
        return false;
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(uint32_t);
    info.pCode = words.data();
    VkShaderModule createdModule = VK_NULL_HANDLE;
    if (!detail::checkResult(createModule(device.handle(), &info, nullptr, &createdModule), "vkCreateShaderModule"))
    {
        std::fprintf(stderr, "Vulkan error: Shader Module creation failed for %s.\n", reinterpret_cast<const char*>(fileName.c_str()));
        return false;
    }
    device_ = device.handle();
    module_ = createdModule;
    destroyModule_ = destroyModule;
    std::fprintf(stderr, "Vulkan: Shader Module created; %s, %zu bytes.\n", reinterpret_cast<const char*>(fileName.c_str()), info.codeSize);
    return true;
}
}
