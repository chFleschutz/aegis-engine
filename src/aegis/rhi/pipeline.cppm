module;
#include <expected>
#include <span>

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
        std::span<char> code;
        std::string_view entryPoint{ "main" };
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
        Format depthAttachment;
    };

    [[nodiscard]] static auto create(const ComputeDesc& desc) -> std::expected<Pipeline, Error>;
    [[nodiscard]] static auto create(const GraphicsDesc& desc) -> std::expected<Pipeline, Error>;

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

    [[nodiscard]] static auto createShaderModule(
        const vk::raii::Device& device,
        std::span<char> code) -> std::expected<vk::raii::ShaderModule, Error>;

    [[nodiscard]] static auto createComputePipeline(
        const vk::raii::Device& device,
        const vk::raii::PipelineLayout& layout,
        const vk::raii::ShaderModule& module,
        std::string_view entryPoint) -> std::expected<vk::raii::Pipeline, Error>;

    [[nodiscard]] static auto createGraphicsPipeline() -> std::expected<vk::raii::Pipeline, Error>;

    vk::raii::Pipeline m_pipeline;
    vk::raii::PipelineLayout m_layout;
    vk::PipelineBindPoint m_bindPoint;
};
}
