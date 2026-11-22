export module aegis.render_passes:ui_pass;
import std;
import :names;
import aegis.render_graph;
import aegis.rhi;

export namespace aegis::render_graph
{
/// @brief Draws ImGui over a "Final" before it is presented, or, rendering the views into targets of their own
///        (Desc::targetFormat), onto the frame's target while reading each view's "Final", so the scene passes come
///        first.
/// @note The ImGui context, its backends (platform and Vulkan: rhi::ImGuiBackend) and the frame's UI belong to the
///       caller: it builds the UI and calls ImGui::Render before the graph executes, so the pass only records the
///       draw data, and a rebuilt graph starts no backend. The backend must outlive the pass.
class UiPass : public RenderPass
{
public:
    struct Desc
    {
        /// @brief The backend the pass records through; needed by createResources, so a graph can be declared without it.
        rhi::ImGuiBackend* backend{ nullptr };
        /// @brief Draw onto the frame's target, of this format and cleared first, and sample every one of 'finals'
        ///        (the views' targets, which the UI shows) instead of drawing over the first.
        std::optional<rhi::Format> targetFormat;
        /// @brief The images the UI draws over (the first) or samples (all, with targetFormat), by their names in the
        ///        graph: a view's is scopedName(view, names::finalImage).
        std::vector<std::string> finals{ std::string{ names::finalImage } };
    };

    UiPass(ResourceDeclarations& declarations, Desc desc);

    [[nodiscard]] auto name() const -> std::string_view override;
    [[nodiscard]] auto createResources(ResourcePool& pool) -> std::expected<void, Error> override;
    auto execute(ResourcePool& pool, const FrameInfo& frameInfo) -> void override;

private:
    rhi::ImGuiBackend* m_backend;
    std::optional<rhi::Format> m_targetFormat; // Drawing onto the frame's target
    std::vector<ResourceId> m_finals;
};
}
