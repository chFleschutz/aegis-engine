module aegis.render_passes;
import std;
import aegis.paths;
import aegis.rhi;

namespace aegis::render_graph
{
auto shaderDirectory() -> std::filesystem::path
{
    return paths::shaders();
}

auto loadShader(std::string_view relativePath) -> std::expected<std::vector<std::uint32_t>, Error>
{
    const auto path = shaderDirectory() / relativePath;
    std::ifstream file{ path, std::ios::binary | std::ios::ate };
    if (!file)
        return makeError(ErrorCode::ShaderLoadFailed, std::format("Failed to open shader '{}'", path.string()));

    const auto size = static_cast<std::size_t>(file.tellg());
    if (size == 0 || size % sizeof(std::uint32_t) != 0)
        return makeError(ErrorCode::ShaderLoadFailed, std::format("Shader '{}' is not SPIR-V (size {})", path.string(), size));

    std::vector<std::uint32_t> code(size / sizeof(std::uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));
    if (!file)
        return makeError(ErrorCode::ShaderLoadFailed, std::format("Failed to read shader '{}'", path.string()));
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
        return makeError(ErrorCode::ShaderLoadFailed, std::format("Failed to create pipeline '{}'", name), pipeline.error());
    return std::move(*pipeline);
}

auto createGraphicsPipeline(rhi::Device& device, rhi::Pipeline::GraphicsDesc desc, std::span<const ShaderEntry> stages)
    -> std::expected<rhi::Pipeline, Error>
{
    // One load per stage: stages may come from different files. Codes outlive pipeline creation.
    std::vector<std::vector<std::uint32_t>> codes;
    codes.reserve(stages.size());
    std::vector<rhi::Pipeline::Shader> shaders;
    shaders.reserve(stages.size());
    for (const auto& [stage, shaderPath, entryPoint] : stages)
    {
        auto code = loadShader(shaderPath);
        if (!code)
            return std::unexpected{ code.error() };
        auto& stored = codes.emplace_back(std::move(*code));
        shaders.push_back({ .name = desc.name, .stage = stage, .code = stored, .entryPoint = entryPoint });
    }
    desc.shaders = shaders;

    auto pipeline = device.createPipeline(desc);
    if (!pipeline)
        return makeError(ErrorCode::ShaderLoadFailed, std::format("Failed to create pipeline '{}'", desc.name), pipeline.error());
    return std::move(*pipeline);
}

auto createGraphicsPipeline(rhi::Device& device, std::string_view shaderPath, rhi::Pipeline::GraphicsDesc desc)
    -> std::expected<rhi::Pipeline, Error>
{
    const std::array stages{
        ShaderEntry{ rhi::ShaderStage::Vertex, shaderPath, "vertexMain" },
        ShaderEntry{ rhi::ShaderStage::Fragment, shaderPath, "fragmentMain" },
    };
    return createGraphicsPipeline(device, desc, stages);
}
}
