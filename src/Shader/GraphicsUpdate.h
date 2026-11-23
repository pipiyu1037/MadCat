#pragma once
#include <expected>
#include <filesystem>
#include <string>

namespace madcat::shader
{
struct GraphicsUpdateOptions
{
    std::filesystem::path cmake, script, outputRoot, sourceRoot, publishedRoot;
};
// Explicit synchronous developer operation (Windows 11). False snapshots and
// validates existing published packages; true compiles captured source files.
// Returns a new, complete immutable directory. Never changes earlier revisions
// or GPU state. Logs/sources/SPIR-V stay under outputRoot for reproduction.
// A graph factory must capture the returned path by value for later rebuilds.
[[nodiscard]] std::expected<std::filesystem::path,std::string> prepareGraphicsUpdate(
    const GraphicsUpdateOptions& options, bool compile);
}
