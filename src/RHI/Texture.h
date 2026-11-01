#pragma once

#include <cstdint>
#include <memory>

namespace madcat::rhi
{
class VulkanDevice;
namespace detail { struct TextureState; struct TextureAccess; }

// Strong reference to a GPU image, view and memory. Copies share one resource;
// they do not grant concurrent write access or establish GPU completion. Keep
// the Device and at least one reference alive until all submitted use finishes.
// Currently only the backend's single-sample depth-attachment factory exists.
class TextureRef
{
public:
    TextureRef() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
    [[nodiscard]] uint32_t width() const noexcept;
    [[nodiscard]] uint32_t height() const noexcept;

private:
    friend struct detail::TextureAccess;
    friend TextureRef createDepthTexture(const VulkanDevice&, uint32_t, uint32_t);
    std::shared_ptr<detail::TextureState> state_;
};
}
