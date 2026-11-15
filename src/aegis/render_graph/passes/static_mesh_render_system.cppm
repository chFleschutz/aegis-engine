export module aegis.render_passes:static_mesh_render_system;
import std;
import :pipeline_cache;
import :render_system;
import aegis.render_graph;
import aegis.assets;
import aegis.rhi;
import aegis.math;

export namespace aegis::render_graph
{
/// @brief Draws every instance of the frame's InstanceStore whose draw batch has one MaterialType,
///        one indexed draw each (the CPU-driven path), with its batch's pipeline for the G-buffer technique
///        (mirrored instances with the clockwise one), built for the batch template's surface shader.
class StaticMeshRenderSystem : public RenderSystem
{
public:
    explicit StaticMeshRenderSystem(assets::MaterialType type = assets::MaterialType::Opaque,
        std::string technique = "cpu-driven/geometry");

    [[nodiscard]] auto createResources(rhi::Device& device, const RenderTargets& targets)
        -> std::expected<void, Error> override;
    /// @brief Creates the pipelines of the frame's draw batches it draws, both front faces of each.
    [[nodiscard]] auto prepare(const FrameScene& frameScene) -> std::expected<void, Error> override;
    auto render(const RenderContext& context) -> void override;

    [[nodiscard]] auto pipelines() const noexcept -> const PipelineCache<>& { return m_pipelines; }

private:
    [[nodiscard]] auto createPipeline(const PipelineKey& key) const -> std::expected<rhi::Pipeline, Error>;

    assets::MaterialType m_type;
    std::string m_technique; // Its shader file without the surface shader's name (techniqueShader)
    rhi::Device* m_device{ nullptr };
    std::vector<rhi::Pipeline::ColorAttachment> m_colorAttachments;
    rhi::Format m_depthAttachment{ rhi::Format::Unknown };
    PipelineCache<> m_pipelines;
};
}
