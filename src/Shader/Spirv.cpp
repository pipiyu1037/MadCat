#include "Spirv.h"

#include <bit>
#include <cstdio>
#include <fstream>
#include <limits>
#include <utility>

namespace madcat::shader
{
bool loadSpirv(const std::filesystem::path& file, std::vector<uint32_t>& words)
{
    const auto fileName = file.u8string();
    const auto reject = [&](const char* reason) {
        std::fprintf(stderr, "Shader error: %s: %s.\n", reinterpret_cast<const char*>(fileName.c_str()), reason);
        return false;
    };
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error))
    {
        return reject("expected a readable regular SPIR-V file");
    }
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) { return reject("cannot open SPIR-V file"); }
    const auto end = input.tellg();
    if (end < std::streampos(0)) { return reject("cannot determine file size"); }
    const auto byteCount = static_cast<uintmax_t>(static_cast<std::streamoff>(end));
    if (byteCount == 0) { return reject("empty SPIR-V file"); }
    if (byteCount % sizeof(uint32_t) != 0) { return reject("SPIR-V byte size must be a multiple of 4"); }
    constexpr size_t headerWords = 5;
    if (byteCount <= headerWords * sizeof(uint32_t)) { return reject("SPIR-V header or instructions are missing"); }
    std::vector<uint32_t> candidate;
    if (byteCount > static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()) ||
        byteCount / sizeof(uint32_t) > candidate.max_size())
    {
        return reject("SPIR-V file is too large to read");
    }
    candidate.resize(static_cast<size_t>(byteCount / sizeof(uint32_t)));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(candidate.data()), static_cast<std::streamsize>(byteCount)))
    {
        return reject("incomplete SPIR-V read");
    }
    if (input.peek() != std::char_traits<char>::eof() || input.bad())
    {
        return reject("SPIR-V file changed size or could not be read completely");
    }
    constexpr uint32_t magic = 0x07230203;
    if (candidate[0] == std::byteswap(magic))
    {
        for (auto& word : candidate) { word = std::byteswap(word); }
    }
    if (candidate[0] != magic) { return reject("invalid SPIR-V magic number"); }
    if ((candidate[1] & 0xff0000ffu) != 0 || candidate[1] < 0x00010000 || candidate[1] > 0x00010600)
    {
        return reject("expected SPIR-V version 1.0 through 1.6");
    }
    if (candidate[3] == 0 || candidate[4] != 0) { return reject("invalid SPIR-V bound or reserved schema word"); }
    for (size_t offset = headerWords; offset < candidate.size();)
    {
        const size_t wordCount = candidate[offset] >> 16;
        if (wordCount == 0 || wordCount > candidate.size() - offset)
        {
            return reject("zero-length or truncated SPIR-V instruction");
        }
        offset += wordCount;
    }
    words = std::move(candidate);
    return true;
}
}
