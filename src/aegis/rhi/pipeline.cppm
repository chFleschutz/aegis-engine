module;
#include <expected>
#include <span>
#include <string_view>

export module aegis.rhi:pipeline;
import :error;
import :common;
import :fwd;
import vulkan_hpp;

export namespace aegis::rhi
{
class Pipeline
{
    friend class Device;

public:
    struct Shader
    {
        std::string_view name;
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

    struct ColorAttachment
    {
        Format format;
        BlendMode blend{ BlendMode::None };
    };

    struct DepthState
    {
        bool test{ true };
        bool write{ true };
        CompareOp compareOp{ CompareOp::Less };
    };

    // Neither Desc names a layout: every pipeline uses the Device's shared one (the bindless set
    // plus CommandBuffer::maxPushConstantSize bytes of push constants, visible to all stages).

    struct ComputeDesc
    {
        std::string_view name;
        Shader shader;
    };

    struct GraphicsDesc
    {
        std::string_view name;
        std::span<Shader> shaders;
        std::span<const ColorAttachment> colorAttachments;
        Format depthAttachment{ Format::Unknown };
        /// @brief Set to the depth format as well when that format has a stencil aspect to use.
        Format stencilAttachment{ Format::Unknown };
        DepthState depth{};
        CullMode cullMode{ CullMode::Back };
        FrontFace frontFace{ FrontFace::CounterClockwise };
        std::span<VertexBinding> vertexBindings;
        std::span<VertexAttribute> vertexAttributes;
    };

    [[nodiscard]] auto operator*() const noexcept -> vk::Pipeline { return *m_pipeline; }
    [[nodiscard]] auto handle() const noexcept -> vk::Pipeline { return *m_pipeline; }
    [[nodiscard]] auto bindPoint() const noexcept -> vk::PipelineBindPoint { return m_bindPoint; }

private:
    [[nodiscard]] static auto create(
        const Device& device,
        const ComputeDesc& desc)
        -> std::expected<Pipeline, Error>;

    [[nodiscard]] static auto create(
        const Device& device,
        const GraphicsDesc& desc)
        -> std::expected<Pipeline, Error>;

    [[nodiscard]] static auto createComputePipeline(
        const vk::raii::Device& device,
        vk::PipelineLayout layout,
        const Shader& shader)
        -> std::expected<vk::raii::Pipeline, Error>;

    [[nodiscard]] static auto createGraphicsPipeline(
        const vk::raii::Device& device,
        vk::PipelineLayout pipelineLayout,
        const GraphicsDesc& desc)
        -> std::expected<vk::raii::Pipeline, Error>;

    [[nodiscard]] static auto createShaderModule(
        const vk::raii::Device& device,
        std::string_view name,
        std::span<std::uint32_t> code)
        -> std::expected<vk::raii::ShaderModule, Error>;

    Pipeline(vk::raii::Pipeline pipeline, vk::PipelineBindPoint bindPoint);

    vk::raii::Pipeline m_pipeline;
    vk::PipelineBindPoint m_bindPoint;
};
}
