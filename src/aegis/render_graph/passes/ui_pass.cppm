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
/// @note The ImGui context and any platform backend (e.g. ImGui_ImplGlfw) belong to the caller, and
///       must outlive the pass.
class UiPass : public RenderPass
{
public:
    struct Desc
    {
        const rhi::Context& context;
        /// @brief Builds the frame's UI: the platform backend's NewFrame, ImGui::NewFrame and the
        ///        windows. The pass then renders it (ImGui::Render ends the frame).
        std::function<void()> build;
    };

    /// @brief Descriptor sets the backend may allocate: the font atlas plus textures the UI shows.
    static constexpr std::uint32_t descriptorPoolSize{ 64 };

    UiPass(ResourceDeclarations& declarations, Desc desc);
    ~UiPass() override;

    [[nodiscard]] auto info() const -> Info override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    const rhi::Context& m_context;
    std::function<void()> m_build;
    ResourceHandle m_final;
    bool m_initialized{ false };
};
}
