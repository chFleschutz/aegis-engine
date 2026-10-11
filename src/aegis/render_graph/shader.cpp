module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <string_view>
#include <vector>

module aegis.render_graph;
import aegis.rhi;

namespace aegis::render_graph
{
auto shaderDirectory() -> std::filesystem::path
{
    return std::filesystem::path{ AEGIS_SHADER_DIR };
}

auto loadShader(std::string_view relativePath) -> std::expected<std::vector<std::uint32_t>, Error>
{
    const auto path = shaderDirectory() / relativePath;
    std::ifstream file{ path, std::ios::binary | std::ios::ate };
    if (!file)
        return makeError(std::format("Failed to open shader '{}'", path.string()));

    const auto size = static_cast<std::size_t>(file.tellg());
    if (size == 0 || size % sizeof(std::uint32_t) != 0)
        return makeError(std::format("Shader '{}' is not SPIR-V (size {})", path.string(), size));

    std::vector<std::uint32_t> code(size / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));
    if (!file)
        return makeError(std::format("Failed to read shader '{}'", path.string()));
    return code;
}

auto createComputePipeline(rhi::Device& device, std::string_view name, std::string_view shaderPath,
    std::string_view entryPoint) -> std::expected<rhi::Pipeline, Error>
{
    auto code = loadShader(shaderPath);
    if (!code)
        return std::unexpected{ code.error() };

    auto pipeline = device.createPipeline(rhi::Pipeline::ComputeDesc{
        .name = name,
        .shader = { .name = name, .stage = rhi::ShaderStage::Compute, .code = *code, .entryPoint = entryPoint },
    });
    if (!pipeline)
        return makeError(pipeline.error(), std::format("Failed to create pipeline '{}'", name));
    return std::move(*pipeline);
}

auto createGraphicsPipeline(rhi::Device& device, std::string_view shaderPath, rhi::Pipeline::GraphicsDesc desc)
    -> std::expected<rhi::Pipeline, Error>
{
    auto code = loadShader(shaderPath);
    if (!code)
        return std::unexpected{ code.error() };

    std::array shaders{
        rhi::Pipeline::Shader{ .name = desc.name, .stage = rhi::ShaderStage::Vertex, .code = *code, .entryPoint = "vertexMain" },
        rhi::Pipeline::Shader{ .name = desc.name, .stage = rhi::ShaderStage::Fragment, .code = *code, .entryPoint = "fragmentMain" },
    };
    desc.shaders = shaders;

    auto pipeline = device.createPipeline(desc);
    if (!pipeline)
        return makeError(pipeline.error(), std::format("Failed to create pipeline '{}'", desc.name));
    return std::move(*pipeline);
}
}
