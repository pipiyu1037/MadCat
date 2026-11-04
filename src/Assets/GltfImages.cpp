#include "GltfImport.h"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <string_view>

namespace madcat::assets::detail
{
namespace
{
constexpr size_t imageLimit = 256 * 1024 * 1024;
struct Failure { std::string message; };
void require(bool valid, std::string_view why) { if (!valid) { throw Failure{std::string(why)}; } }

std::vector<std::byte> readImage(const cgltf_image& image, const std::filesystem::path& file)
{
    if (image.buffer_view)
    {
        require(!image.uri && image.mime_type, "bufferView image requires a MIME type and no URI");
        const auto* begin = reinterpret_cast<const std::byte*>(cgltf_buffer_view_data(image.buffer_view));
        require(begin && image.buffer_view->size <= imageLimit, "missing or oversized bufferView image");
        return {begin, begin + image.buffer_view->size};
    }
    require(image.uri, "image has no URI or bufferView");
    const std::string_view uri(image.uri);
    if (uri.starts_with("data:"))
    {
        const auto comma = uri.find(',');
        require(comma != uri.npos && (uri.substr(0, comma) == "data:image/png;base64" || uri.substr(0, comma) == "data:image/jpeg;base64"),
            "image data URI must be PNG/JPEG base64");
        const auto encoded = uri.substr(comma + 1);
        require(!encoded.empty() && encoded.size() % 4 == 0 && encoded.size() <= imageLimit, "invalid image base64 length");
        const auto padding = encoded.ends_with("==") ? 2u : encoded.ends_with('=') ? 1u : 0u;
        const auto size = encoded.size() / 4 * 3 - padding;
        cgltf_options options{};
        void* decoded = nullptr;
        const auto result = cgltf_load_buffer_base64(&options, size, encoded.data(), &decoded);
        if (result == cgltf_result_out_of_memory) { throw std::bad_alloc{}; }
        require(result == cgltf_result_success, "invalid image base64 data");
        const std::unique_ptr<void, decltype(&std::free)> storage(decoded, &std::free);
        const auto* begin = static_cast<const std::byte*>(storage.get());
        return {begin, begin + size};
    }
    require(uri.find_first_of(":?#") == uri.npos, "unsupported image URI");
    std::string decoded(uri);
    decoded.resize(cgltf_decode_uri(decoded.data()));
    require(decoded.find('\0') == decoded.npos, "image URI contains NUL");
    const auto relative = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(decoded.data()), decoded.size()));
    std::ifstream input(file.parent_path() / relative, std::ios::binary | std::ios::ate);
    require(bool(input), "cannot open external image: " + decoded);
    const auto size = input.tellg();
    require(size > 0 && size <= static_cast<std::streamoff>(imageLimit), "external image is empty or oversized");
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    input.seekg(0);
    require(bool(input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))), "cannot read external image");
    return bytes;
}

void validateMime(const cgltf_image& image, std::span<const std::byte> bytes)
{
    std::string_view mime = image.mime_type ? image.mime_type : "";
    if (image.uri && std::string_view(image.uri).starts_with("data:"))
    {
        const std::string_view uri(image.uri);
        const auto declared = uri.substr(5, uri.find(';') - 5);
        require(mime.empty() || mime == declared, "image MIME declarations disagree");
        mime = declared;
    }
    if (mime.empty()) { return; }
    const bool png = bytes.size() >= 8 && bytes[0] == std::byte{0x89} && bytes[1] == std::byte{'P'} && bytes[2] == std::byte{'N'} && bytes[3] == std::byte{'G'};
    const bool jpeg = bytes.size() >= 3 && bytes[0] == std::byte{0xff} && bytes[1] == std::byte{0xd8} && bytes[2] == std::byte{0xff};
    require((mime == "image/png" && png) || (mime == "image/jpeg" && jpeg), "image MIME type does not match PNG/JPEG bytes");
}
}

std::expected<void, std::string> importImages(const cgltf_data& data, const std::filesystem::path& file, GltfGeometry& output)
{
    for (size_t index = 0; index < data.images_count; ++index)
    {
        try
        {
            const auto& source = data.images[index];
            const auto bytes = readImage(source, file);
            validateMime(source, bytes);
            auto image = decodeImage(bytes);
            if (!image) { throw Failure{image.error()}; }
            image->name = source.name ? source.name : "";
            output.images.push_back(std::move(*image));
        }
        catch (const Failure& failure) { return std::unexpected("image " + std::to_string(index) + ": " + failure.message); }
    }
    return {};
}
}
