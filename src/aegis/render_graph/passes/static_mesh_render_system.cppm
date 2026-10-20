export module aegis.render_graph:static_mesh_render_system;
import std;
import :error;
import :render_system;
import aegis.assets;
import aegis.world.render_components;
import aegis.rhi;
import aegis.math;

export namespace aegis::render_graph
{
/// @brief Draws every entity with an aegis::MeshRenderer of one MaterialType,
///        one indexed draw each (the CPU-driven path). Materials use the default PBR parameter block.
class StaticMeshRenderSystem : public RenderSystem
{
public:
    explicit StaticMeshRenderSystem(assets::MaterialType type = assets::MaterialType::Opaque,
        std::string shader = "cpu-driven/geometry.slang.spv");

    [[nodiscard]] auto createResources(rhi::Device& device, const RenderTargets& targets)
        -> std::expected<void, Error> override;
    auto render(const RenderContext& context) -> void override;

private:
    assets::MaterialType m_type;
    std::string m_shader;
    rhi::Device* m_device{ nullptr };
    std::optional<rhi::Pipeline> m_pipeline;
};
}
