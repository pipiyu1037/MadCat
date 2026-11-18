#pragma once
#include "RHI/Texture.h"
#include <vulkan/vulkan.h>
#include <functional>
#include <memory>

struct GLFWwindow;
namespace madcat::rhi
{
// Single context/window. Runtime owns this adapter and retires frame commands
// before resetting it. Only the static font atlas may be sampled in this slice.
class VulkanUi
{
public:
    VulkanUi();
    ~VulkanUi();
    VulkanUi(const VulkanUi&) = delete;
    VulkanUi& operator=(const VulkanUi&) = delete;
    bool initialize(GLFWwindow* window);
    bool prepare(const VulkanDevice& device, VkFormat format, uint32_t imageCount);
    void resetRenderer(); // requires completed GPU work and retired commands
    void frame(const std::function<void()>& draw);
    bool capturesKeyboard() const;
    bool record(VkCommandBuffer command, VkFormat format);
    TextureRef font() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
