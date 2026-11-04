#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <cstddef>

namespace madcat::rhi
{
class VulkanDevice;
class TextureBindingRef;
namespace detail { struct TextureState; struct TextureAccess; struct TextureBindingState; struct TextureBindingAccess; }
struct SamplerDesc
{
    enum class Wrap { Repeat, Clamp, Mirror };
    enum class Mips { None, Nearest, Linear };
    bool linearMag = true, linearMin = true;
    Wrap wrapU = Wrap::Repeat, wrapV = Wrap::Repeat;
    Mips mips = Mips::Linear;
};

// Strong reference to a GPU image, view and memory. Copies share one resource;
// they do not grant concurrent write access or establish GPU completion. Keep
// the Device and at least one reference alive until all submitted use finishes.
// Backend factories provide depth attachments or immutable sampled RGBA images.
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
    friend TextureRef createSampledTexture(const VulkanDevice&, uint32_t, uint32_t, std::span<const uint8_t>);
    friend TextureBindingRef createTextureBinding(const VulkanDevice&, TextureRef, const SamplerDesc&);
    std::shared_ptr<detail::TextureState> state_;
};

// Immutable sampled-image/sampler binding. Owns descriptor storage, sampler and
// a texture reference. Never update/reuse it until all submitted use completes.
class TextureBindingRef
{
public:
    TextureBindingRef() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
private:
    friend struct detail::TextureBindingAccess;
    friend TextureBindingRef createTextureBinding(const VulkanDevice&, TextureRef, const SamplerDesc&);
    std::shared_ptr<detail::TextureBindingState> state_;
};
}
