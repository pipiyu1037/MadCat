#pragma once
#include "RHI/AccelerationStructure.h"
#include "RHI/Buffer.h"
#include <span>
#include <vulkan/vulkan.h>

namespace madcat::rhi { class VulkanDevice; }
namespace madcat::rhi::detail
{
struct AccelerationAccess
{
    static AccelerationRef bottom(const VulkanDevice&,BufferRef vertices,BufferRef indices,uint32_t firstIndex,uint32_t indexCount);
    static AccelerationRef top(const VulkanDevice&,std::span<const AccelerationRef> bottom,uint32_t capacity);
    static bool buildBottom(VkCommandBuffer,const AccelerationRef&,const BufferRef&,const BufferRef&);
    static bool buildTop(VkCommandBuffer,const AccelerationRef&,std::span<const AccelerationRef>,std::span<const AccelerationInstance>);
    static VkAccelerationStructureKHR handle(const AccelerationRef&);
    static VkDevice device(const AccelerationRef&);
    static bool isTop(const AccelerationRef&);
};
}
