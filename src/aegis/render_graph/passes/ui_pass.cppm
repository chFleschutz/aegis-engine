export module aegis.render_graph:ui_pass;
import std;
import :error;
import :render_pass;
import :resource;
import :declarations;
import :resource_pool;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Draws ImGui over "Final" before it is presented. Owns ImGui's Vulkan backend on the
///        device: initialized in createResources, shut down with the pass.
/// @note The ImGui context, any platform backend (e.g. ImGui_ImplGlfw) and the frame's UI belong to the
///       caller: it builds the UI and calls ImGui::Render before the graph executes, so the pass only
///       records the draw data. The context must outlive the pass.
class UiPass : public RenderPass
{
public:
    struct Desc
    {
        /// @brief ImGui's backend takes the instance; needed by createResources, so a graph can be declared without it.
        const rhi::Context* context{ nullptr };
    };

    /// @brief Descriptor sets the backend may allocate: the font atlas plus textures the UI shows.
    static constexpr std::uint32_t descriptorPoolSize{ 64 };

    UiPass(ResourceDeclarations& declarations, Desc desc);
    ~UiPass() override;

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const rhi::Context* m_context;
    ResourceId m_final;
    bool m_initialized{ false };
};
}
