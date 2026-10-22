#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>

namespace madcat::rhi
{
class VulkanDevice;

// Owns one module, borrowing a Device that must outlive it. No copying/moving.
// Finish host operations using the module before destruction. Pipelines do not
// retain the module: destroying it does not require waiting for their GPU work.
class VulkanShaderModule
{
public:
    VulkanShaderModule() = default;
    ~VulkanShaderModule();

    VulkanShaderModule(const VulkanShaderModule&) = delete;
    VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;
    VulkanShaderModule(VulkanShaderModule&&) = delete;
    VulkanShaderModule& operator=(VulkanShaderModule&&) = delete;

    // Load offline-validated SPIR-V and create a module. Basic file checks do
    // not replace spirv-val or capability/entry checks during pipeline creation.
    // Failure leaves an empty owner; repeated initialization preserves its module.
    [[nodiscard]] bool initialize(const VulkanDevice& device, const std::filesystem::path& file);

    // Borrowed handle; invalid after this owner is destroyed.
    [[nodiscard]] VkShaderModule handle() const { return module_; }
    [[nodiscard]] VkDevice deviceHandle() const { return device_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkShaderModule module_ = VK_NULL_HANDLE;
    PFN_vkDestroyShaderModule destroyModule_ = nullptr;
};
}
