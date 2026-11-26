#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace madcat::rhi
{
class VulkanDevice;
namespace detail { struct BufferState; struct BufferAccess; }

// Strong reference to GPU buffer data initialized once by the host. Storage
// buffers may subsequently be written by the GPU with explicit synchronization.
// Copies share one allocation; resetting a reference releases only that owner's
// use. The Device must outlive
// every reference. The last reference may be released only after GPU use has
// finished; a CPU reference count alone does not establish GPU completion.
class BufferRef
{
public:
    BufferRef() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
    [[nodiscard]] std::size_t sizeBytes() const noexcept;
    // Zero unless addressable. Borrowed GPU address, valid only while retaining
    // this allocation and its Device; an address alone does not keep it alive.
    [[nodiscard]] uint64_t deviceAddress() const noexcept;

private:
    friend struct detail::BufferAccess;
    friend BufferRef createVertexBuffer(const VulkanDevice&, std::span<const std::byte>, bool);
    friend BufferRef createIndexBuffer(const VulkanDevice&, std::span<const uint32_t>, bool);
    friend BufferRef createStorageBuffer(const VulkanDevice&, std::span<const std::byte>, bool);
    std::shared_ptr<detail::BufferState> state_;
};
}
