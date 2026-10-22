#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace madcat::shader
{
// Read an offline-validated SPIR-V file into aligned, native-endian words.
// Checks the header (SPIR-V 1.0 through 1.6) and instruction boundaries, not
// semantic validity, entry points or Device capabilities. Run spirv-val first.
// Success replaces words; failure preserves it. Allocation errors may throw.
[[nodiscard]] bool loadSpirv(const std::filesystem::path& file, std::vector<uint32_t>& words);
}
