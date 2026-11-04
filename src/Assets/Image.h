#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace madcat::assets
{
// Owned, tightly packed RGBA8, top row first, straight alpha. RGB bytes retain
// image encoding; texture usage determines sRGB interpretation, not the decoder.
struct Image
{
    std::string name;
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
};

// stb_image PNG/JPEG adapter. Maximum encoded/decoded storage 256 MiB, edge
// 16384. No orientation/color-profile transform or premultiplication.
[[nodiscard]] std::expected<Image, std::string> decodeImage(std::span<const std::byte> encoded);
}
