export module aegis.render_passes:static_mesh_render_system;
import std;
import :draw_order;
import :frame_scene;
import :pipeline_cache;
import :render_system;
import aegis.render_graph;
import aegis.assets;
import aegis.rhi;
import aegis.math;

export namespace aegis::render_graph
{
/// @brief Draws every instance of the frame's InstanceStore whose draw batch its technique draws (drawsBatch: for the
///        G-buffer and depth, the unblended batches of one MaterialType; forward, the blended ones), one indexed draw
///        each (the CPU-driven path), with its batch's pipeline for its technique (mirrored instances with the
///        clockwise one), built for the batch template's surface shader: from cpu-driven/geometry.slang into the
///        G-buffer, or depth only for a shadow view; from cpu-driven/forward.slang lit and blended into the scene
///        color, with the lighting block of its pass (RenderContext::lighting), back to front by the view depth of each
///        instance's bounding-sphere centre (RenderContext::view), so the blends compose.
class StaticMeshRenderSystem : public RenderSystem
{
public:
    /// @brief The shader file the G-buffer and depth techniques draw with (techniqueShader).
    static constexpr std::string_view shaderFile{ "cpu-driven/geometry" };
    /// @brief The shader file the forward technique draws with.
    static constexpr std::string_view forwardShaderFile{ "cpu-driven/forward" };

    explicit StaticMeshRenderSystem(Technique technique = Technique::GBuffer,
        assets::MaterialType type = assets::MaterialType::Opaque);

    [[nodiscard]] auto createResources(rhi::Device& device, const RenderTargets& targets)
        -> std::expected<void, Error> override;
    /// @brief Creates the pipelines of the frame's draw batches it draws, both front faces of each.
    [[nodiscard]] auto prepare(const FrameScene& frameScene) -> std::expected<void, Error> override;
    auto render(const RenderContext& context) -> void override;

    [[nodiscard]] auto technique() const noexcept -> Technique { return m_technique; }
    [[nodiscard]] auto pipelines() const noexcept -> const PipelineCache<>& { return m_pipelines; }

private:
    [[nodiscard]] auto createPipeline(const PipelineKey& key) const -> std::expected<rhi::Pipeline, Error>;

    Technique m_technique;
    assets::MaterialType m_type;
    rhi::Device* m_device{ nullptr };
    std::vector<rhi::Pipeline::ColorAttachment> m_colorAttachments;
    rhi::Format m_depthAttachment{ rhi::Format::Unknown };
    PipelineCache<> m_pipelines;
    std::vector<detail::DepthSortedDraw> m_order; // Forward: this frame's draws, back to front (indices: static, then dynamic)
};
}
