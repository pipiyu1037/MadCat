#include "Image.h"

#include <stb_image.h>

#include <memory>

namespace madcat::assets
{
std::expected<Image, std::string> decodeImage(std::span<const std::byte> encoded)
{
    constexpr size_t limit = 256 * 1024 * 1024;
    if (encoded.empty() || encoded.size() > limit) { return std::unexpected("empty or oversized encoded image"); }
    const auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.data());
    const auto size = static_cast<int>(encoded.size());
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(bytes, size, &width, &height, &channels))
    {
        return std::unexpected("invalid or unsupported PNG/JPEG image");
    }
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || uint64_t(width) * height * 4 > limit)
    {
        return std::unexpected("image extent exceeds supported nonzero limits");
    }
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes, size, &width, &height, &channels, STBI_rgb_alpha), &stbi_image_free);
    if (!pixels)
    {
        const auto* reason = stbi_failure_reason();
        return std::unexpected(std::string("image decode failed: ") + (reason ? reason : "unknown stb_image error"));
    }
    Image image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.rgba.assign(pixels.get(), pixels.get() + size_t(width) * height * 4);
    return image;
}
}
