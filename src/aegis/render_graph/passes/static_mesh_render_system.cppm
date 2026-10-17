export module aegis.render_graph:static_mesh_render_system;
import std;
import :error;
import :render_system;
import aegis.assets;
import aegis.rhi;
import aegis.math;

export namespace aegis::render_graph
{
/// @brief Draws every entity with an assets::Mesh and an assets::Material of one MaterialType,
///        one indexed draw each (the CPU-driven path). Materials use the default PBR parameter block.
class StaticMeshRenderSystem : public RenderSystem
{
public:
    /// @brief cpu-driven/geometry.slang's push constants.
    struct PushConstants
    {
        glm::mat3x4 model; // Top three rows
        glm::vec3 normalRow0;
        glm::vec3 normalRow1;
        glm::vec3 normalRow2;
        rhi::DeviceAddress camera;
        rhi::DeviceAddress mesh;
        rhi::DeviceAddress material;
    };
    static_assert(sizeof(PushConstants) <= rhi::CommandBuffer::maxPushConstantSize);

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
