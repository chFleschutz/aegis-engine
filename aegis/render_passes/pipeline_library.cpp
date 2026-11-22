module;
#include <cassert>

module aegis.render_passes;
import std;
import aegis.assets;
import aegis.rhi;

namespace aegis::render_graph
{
PipelineLibrary::PipelineLibrary() = default;
PipelineLibrary::~PipelineLibrary() = default;

auto PipelineLibrary::bind(rhi::Device& device) -> void
{
    assert((!m_device || m_device == &device) && "A pipeline library serves one device");
    m_device = &device;
}

auto PipelineLibrary::compute(rhi::Device& device, std::string_view name, std::string_view shaderPath,
    std::string_view entryPoint) -> std::expected<const rhi::Pipeline*, Error>
{
    bind(device);
    auto key = detail::computePipelineKey(shaderPath, entryPoint);
    if (const auto found = m_pipelines.find(key); found != m_pipelines.end())
        return &found->second;

    auto pipeline = createComputePipeline(device, name, shaderPath, entryPoint);
    if (!pipeline)
        return std::unexpected{ std::move(pipeline.error()) };
    return &m_pipelines.emplace(std::move(key), std::move(*pipeline)).first->second;
}

auto PipelineLibrary::graphics(rhi::Device& device, const rhi::Pipeline::GraphicsDesc& desc,
    std::span<const ShaderEntry> stages) -> std::expected<const rhi::Pipeline*, Error>
{
    bind(device);
    auto key = detail::graphicsPipelineKey(desc, stages);
    if (const auto found = m_pipelines.find(key); found != m_pipelines.end())
        return &found->second;

    auto pipeline = createGraphicsPipeline(device, desc, stages);
    if (!pipeline)
        return std::unexpected{ std::move(pipeline.error()) };
    return &m_pipelines.emplace(std::move(key), std::move(*pipeline)).first->second;
}

auto PipelineLibrary::graphics(rhi::Device& device, std::string_view shaderPath, const rhi::Pipeline::GraphicsDesc& desc)
    -> std::expected<const rhi::Pipeline*, Error>
{
    const std::array stages{
        ShaderEntry{ rhi::ShaderStage::Vertex, shaderPath, "vertexMain" },
        ShaderEntry{ rhi::ShaderStage::Fragment, shaderPath, "fragmentMain" },
    };
    return graphics(device, desc, stages);
}

auto PipelineLibrary::brdfLut(rhi::Device& device) -> std::expected<std::shared_ptr<assets::Texture>, Error>
{
    bind(device);
    if (m_brdfLut)
        return m_brdfLut;

    if (!m_ibl)
    {
        auto ibl = Ibl::create(device);
        if (!ibl)
            return std::unexpected{ std::move(ibl.error()) };
        m_ibl.emplace(std::move(*ibl));
    }
    auto lut = m_ibl->brdfLut();
    if (!lut)
        return std::unexpected{ std::move(lut.error()) };
    m_brdfLut = std::move(*lut);
    return m_brdfLut;
}
}

namespace aegis::render_graph::detail
{
auto computePipelineKey(std::string_view shaderPath, std::string_view entryPoint) -> std::string
{
    return std::format("compute|{}:{}", shaderPath, entryPoint);
}

auto graphicsPipelineKey(const rhi::Pipeline::GraphicsDesc& desc, std::span<const ShaderEntry> stages) -> std::string
{
    std::string key{ "graphics" };
    auto out = std::back_inserter(key);
    for (const auto& [stage, shaderPath, entryPoint] : stages)
        std::format_to(out, "|stage {}:{}:{}", std::to_underlying(stage), shaderPath, entryPoint);
    for (const auto& attachment : desc.colorAttachments)
        std::format_to(out, "|color {}:{}", std::to_underlying(attachment.format), std::to_underlying(attachment.blend));
    std::format_to(out, "|depth {}:{}|test {}:{}:{}|raster {}:{}", std::to_underlying(desc.depthAttachment),
        std::to_underlying(desc.stencilAttachment), desc.depth.test, desc.depth.write,
        std::to_underlying(desc.depth.compareOp), std::to_underlying(desc.cullMode), std::to_underlying(desc.frontFace));
    for (const auto& binding : desc.vertexBindings)
        std::format_to(out, "|binding {}:{}", binding.binding, binding.stride);
    for (const auto& attribute : desc.vertexAttributes)
        std::format_to(out, "|attribute {}:{}:{}:{}", attribute.binding, attribute.location,
            std::to_underlying(attribute.format), attribute.offset);
    return key;
}
}
