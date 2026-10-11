module;
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

export module aegis.render_graph:shader;
import :error;

export namespace aegis::render_graph
{
/// @brief Where the build writes compiled shaders (shaders/**/*.slang -> <dir>/**/*.slang.spv).
[[nodiscard]] auto shaderDirectory() -> std::filesystem::path;

/// @brief Loads a compiled shader, e.g. "rhi/ibl/brdf_lut.slang.spv", relative to shaderDirectory().
[[nodiscard]] auto loadShader(std::string_view relativePath) -> std::expected<std::vector<std::uint32_t>, Error>;
}
