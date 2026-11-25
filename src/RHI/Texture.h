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
enum class TextureEncoding { Srgb, Linear };
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
// Backend factories provide depth/HDR color attachments (optionally sampled)
// or immutable sampled RGBA images. A binding does not initialize image contents.
class TextureRef
{
public:
    TextureRef() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
    [[nodiscard]] uint32_t width() const noexcept;
    [[nodiscard]] uint32_t height() const noexcept;

private:
    friend struct detail::TextureAccess;
    friend TextureRef createDepthTexture(const VulkanDevice&, uint32_t, uint32_t, bool);
    friend TextureRef createColorTexture(const VulkanDevice&, uint32_t, uint32_t, bool);
    friend TextureRef createSampledTexture(const VulkanDevice&, uint32_t, uint32_t, std::span<const uint8_t>, TextureEncoding);
    friend TextureBindingRef createTextureBinding(const VulkanDevice&, TextureRef, const SamplerDesc&);
    std::shared_ptr<detail::TextureState> state_;
};

// Immutable sampled binding: a single image/sampler or the fixed MR/normal pair.
// Owns descriptor storage and retains its textures/samplers directly or through
// source bindings. Never update/reuse it until all submitted use completes.
class TextureBindingRef
{
public:
    TextureBindingRef() = default;
    [[nodiscard]] explicit operator bool() const noexcept { return state_ != nullptr; }
private:
    friend struct detail::TextureBindingAccess;
    friend TextureBindingRef createTextureBinding(const VulkanDevice&, TextureRef, const SamplerDesc&);
    friend TextureBindingRef createMaterialDataBinding(const VulkanDevice&, TextureBindingRef, TextureBindingRef);
    std::shared_ptr<detail::TextureBindingState> state_;
};

// Fixed forward-material inputs. data contains MR (image0/sampler1) and normal
// (image2/sampler3), keeping four descriptor sets. Both retain immutable sources.
struct MaterialBinding
{
    TextureBindingRef baseColor;
    TextureBindingRef data;
};
}
