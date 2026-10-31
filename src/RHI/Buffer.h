#pragma once

#include <cstddef>
#include <memory>
#include <span>

namespace madcat::rhi
{
class VulkanDevice;
namespace detail { struct BufferState; struct BufferAccess; }

// Strong reference to immutable GPU buffer data. Copies share one allocation;
// resetting a reference releases only that owner's use. The Device must outlive
// every reference. The last reference may be released only after GPU use has
// finished; a CPU reference count alone does not establish GPU completion.
class BufferRef
{
public:
    BufferRef() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
    [[nodiscard]] std::size_t sizeBytes() const noexcept;

private:
    friend struct detail::BufferAccess;
    friend BufferRef createVertexBuffer(const VulkanDevice&, std::span<const std::byte>);
    std::shared_ptr<detail::BufferState> state_;
};
}
