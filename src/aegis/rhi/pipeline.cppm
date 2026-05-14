module;
#include <expected>
#include <span>
#include <string_view>

export module aegis.rhi:pipeline;
import :device;
import :error;
import :common;
import vulkan_hpp;

export namespace aegis::rhi
{
class Pipeline
{
public:
    struct Shader
    {
        ShaderStage stage;
        std::span<uint32_t> code;
        std::string_view entryPoint{ "main" };
    };

    struct VertexAttribute
    {
        std::uint32_t binding;
        std::uint32_t location;
        Format format;
        std::uint32_t offset;
    };

    struct VertexBinding
    {
        std::uint32_t binding;
        std::uint32_t stride;
    };

    struct ComputeDesc
    {
        const Device& device;
        std::span<vk::DescriptorSetLayout> setLayouts;
        std::span<vk::PushConstantRange> pushConstantRanges;
        Shader shader;
    };

    struct GraphicsDesc
    {
        const Device& device;
        std::span<vk::DescriptorSetLayout> setLayouts;
        std::span<vk::PushConstantRange> pushConstantRanges;
        std::span<Shader> shaders;
        std::span<Format> colorAttachments;
        Format depthAttachment{ Format::Unknown };
        std::span<VertexBinding> vertexBindings;
        std::span<VertexAttribute> vertexAttributes;
    };

    [[nodiscard]] static auto create(const ComputeDesc& desc)
        -> std::expected<Pipeline, Error>;

    [[nodiscard]] static auto create(const GraphicsDesc& desc)
        -> std::expected<Pipeline, Error>;

    [[nodiscard]] auto bindPoint() const
        -> vk::PipelineBindPoint { return m_bindPoint; }

    [[nodiscard]] auto pipeline() const
        -> vk::Pipeline { return *m_pipeline; }

private:
    Pipeline(
        vk::raii::Pipeline pipeline,
        vk::raii::PipelineLayout layout,
        vk::PipelineBindPoint bindPoint);

    [[nodiscard]] static auto createPipelineLayout(
        const vk::raii::Device& device,
        std::span<vk::DescriptorSetLayout> setLayouts,
        std::span<vk::PushConstantRange> pushConstantRanges)
        -> std::expected<vk::raii::PipelineLayout, Error>;

    [[nodiscard]] static auto createComputePipeline(
        const ComputeDesc& desc,
        const vk::raii::PipelineLayout& layout)
        -> std::expected<vk::raii::Pipeline, Error>;

    [[nodiscard]] static auto createGraphicsPipeline(
        const GraphicsDesc& desc,
        const vk::raii::PipelineLayout& pipelineLayout)
        -> std::expected<vk::raii::Pipeline, Error>;

    [[nodiscard]] static auto createShaderModule(
        const vk::raii::Device& device,
        std::span<std::uint32_t> code)
        -> std::expected<vk::raii::ShaderModule, Error>;

    vk::raii::Pipeline m_pipeline;
    vk::raii::PipelineLayout m_layout;
    vk::PipelineBindPoint m_bindPoint;
};
}
