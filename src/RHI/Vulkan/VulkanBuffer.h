#pragma once

#include "RHI/Buffer.h"

#include <vulkan/vulkan.h>

namespace madcat::rhi
{
// Backend factory: dedicated HOST_VISIBLE | HOST_COHERENT allocation, initialized
// once before publication. No staging copy or subsequent host writes. Failure
// returns an empty reference and releases partial Vulkan resources; CPU allocation
// failures may throw. Submit reads after this call returns on the same host thread.
// rayQueryInput additionally enables storage, device-address and AS build-input
// usage. Requires a Device created with enableRayQuery; no AS is built here.
[[nodiscard]] BufferRef createVertexBuffer(const VulkanDevice& device, std::span<const std::byte> data, bool rayQueryInput = false);

// Tightly packed uint32 indices, immutable after upload. Nonempty count must fit
// uint32. Values are limited by maxDrawIndexedIndexValue and 0x00ffffff because
// Device does not enable fullDrawIndexUint32. Topology and vertex bounds are
// checked when pairing with a pipeline.
[[nodiscard]] BufferRef createIndexBuffer(const VulkanDevice& device, std::span<const uint32_t> data, bool rayQueryInput = false);

// Initialized storage, optionally addressable (requires enableRayQuery). Same
// dedicated coherent allocation and one-time host upload as geometry. No mapping,
// readback, descriptor or implicit synchronization is provided after publication.
[[nodiscard]] BufferRef createStorageBuffer(const VulkanDevice& device, std::span<const std::byte> data, bool deviceAddress = false);

namespace detail
{
// Native access stays inside the Vulkan backend; ordinary RHI users keep BufferRef.
struct BufferAccess
{
    [[nodiscard]] static VkBuffer handle(const BufferRef& buffer) noexcept;
    [[nodiscard]] static VkDevice device(const BufferRef& buffer) noexcept;
    [[nodiscard]] static VkBufferUsageFlags usage(const BufferRef& buffer) noexcept;
    [[nodiscard]] static uint32_t maximumIndex(const BufferRef& buffer) noexcept;
    // Backend only: caller has completed GPU writes and a HostRead dependency.
    [[nodiscard]] static bool read(const BufferRef&, std::span<std::byte> destination);
    // Dedicated AS storage/scratch or coherent instance input. Uninitialized;
    // caller declares first GPU use and retains it through GPU completion.
    [[nodiscard]] static BufferRef allocate(const VulkanDevice&, size_t bytes, VkBufferUsageFlags, bool hostVisible=false);
    // Caller has completed previous GPU reads. Writes the supplied prefix.
    [[nodiscard]] static bool write(const BufferRef&, std::span<const std::byte>);
};
}
}
