#pragma once
#include "RHI/ComputePipeline.h"
#include "RHI/Buffer.h"
#include "RHI/AccelerationStructure.h"
#include <vulkan/vulkan.h>

namespace madcat::rhi { class VulkanDevice; }
namespace madcat::rhi::detail
{
struct ComputeBindingState;
using ComputeBinding = std::shared_ptr<ComputeBindingState>;
struct ComputePipelineAccess
{
    static ComputePipelineRef create(const VulkanDevice&, const ComputePipelineDesc&);
    static ComputeBinding bind(const VulkanDevice&, const ComputePipelineRef&, std::span<const BufferRef>,const AccelerationRef& = {});
    static bool matches(const ComputeBinding&, const ComputePipelineRef&, std::span<const BufferRef>,const AccelerationRef& = {});
    static bool counts(const ComputePipelineRef&, size_t reads, size_t writes,bool acceleration=false);
    static bool dispatch(VkCommandBuffer, const ComputeBinding&, uint32_t x, uint32_t y, uint32_t z);
};
}
