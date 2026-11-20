export module aegis.render_passes:static_mesh_render_system;
import std;
import :frame_scene;
import :pipeline_cache;
import :render_system;
import aegis.render_graph;
import aegis.assets;
import aegis.rhi;
import aegis.math;

export namespace aegis::render_graph
{
/// @brief Draws every instance of the frame's InstanceStore whose draw batch has one MaterialType,
///        one indexed draw each (the CPU-driven path), with its batch's pipeline for its technique (mirrored
///        instances with the clockwise one), built from cpu-driven/geometry.slang for the batch template's surface
///        shader: into the G-buffer, or depth only for a shadow view.
class StaticMeshRenderSystem : public RenderSystem
{
public:
    /// @brief The shader file both techniques draw with (techniqueShader).
    static constexpr std::string_view shaderFile{ "cpu-driven/geometry" };

    explicit StaticMeshRenderSystem(Technique technique = Technique::GBuffer,
        assets::MaterialType type = assets::MaterialType::Opaque);

    [[nodiscard]] auto createResources(rhi::Device& device, const RenderTargets& targets)
        -> std::expected<void, Error> override;
    /// @brief Creates the pipelines of the frame's draw batches it draws, both front faces of each.
    [[nodiscard]] auto prepare(const FrameScene& frameScene) -> std::expected<void, Error> override;
    auto render(const RenderContext& context) -> void override;

    [[nodiscard]] auto pipelines() const noexcept -> const PipelineCache<>& { return m_pipelines; }

private:
    [[nodiscard]] auto createPipeline(const PipelineKey& key) const -> std::expected<rhi::Pipeline, Error>;

    Technique m_technique;
    assets::MaterialType m_type;
    rhi::Device* m_device{ nullptr };
    std::vector<rhi::Pipeline::ColorAttachment> m_colorAttachments;
    rhi::Format m_depthAttachment{ rhi::Format::Unknown };
    PipelineCache<> m_pipelines;
};
}
