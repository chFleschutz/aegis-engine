module;
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

export module aegis.render_graph:shader;
import :error;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Where the build writes compiled shaders (shaders/**/*.slang -> <dir>/**/*.slang.spv).
[[nodiscard]] auto shaderDirectory() -> std::filesystem::path;

/// @brief Loads a compiled shader, e.g. "rhi/ibl/brdf_lut.slang.spv", relative to shaderDirectory().
[[nodiscard]] auto loadShader(std::string_view relativePath) -> std::expected<std::vector<std::uint32_t>, Error>;

/// @brief A compute pipeline from a compiled shader (see loadShader).
[[nodiscard]] auto createComputePipeline(rhi::Device& device, std::string_view name, std::string_view shaderPath,
    std::string_view entryPoint = "main") -> std::expected<rhi::Pipeline, Error>;

/// @brief A graphics pipeline from one compiled shader holding both stages ("vertexMain" and
///        "fragmentMain"). 'desc.shaders' is filled in here.
[[nodiscard]] auto createGraphicsPipeline(rhi::Device& device, std::string_view shaderPath,
    rhi::Pipeline::GraphicsDesc desc) -> std::expected<rhi::Pipeline, Error>;

/// @brief Workgroups to cover 'size' threads with groups of 'groupSize'.
[[nodiscard]] constexpr auto groupCount(std::uint32_t size, std::uint32_t groupSize) noexcept -> std::uint32_t
{
    return (size + groupSize - 1) / groupSize;
}
}
