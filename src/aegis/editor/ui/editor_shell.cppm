export module aegis.editor:editor_shell;
import std;
import aegis.editor.core;
import aegis.engine;
import aegis.rhi;
import :panel;

export namespace aegis::editor
{
/// @brief The editor flavor's shell: the world stays in edit mode (no scripts run), and the editor's panels
///        read and edit it at the frame's safe point. The window layout is kept in
///        `<project>/.aegis/editor/imgui.ini` (none when headless or without a project).
class EditorShell final : public Shell
{
public:
    EditorShell();
    EditorShell(const EditorShell&) = delete;
    EditorShell(EditorShell&&) = delete;
    ~EditorShell() override;

    auto operator=(const EditorShell&) -> EditorShell& = delete;
    auto operator=(EditorShell&&) -> EditorShell& = delete;

    auto onStartup(Engine& engine) -> std::expected<void, EngineError> override;
    void onFrame(FrameContext& frame) override;
    [[nodiscard]] auto updateMode() const -> world::UpdateMode override { return world::UpdateMode::Edit; }
    /// @brief The input the editor's UI uses (ImGui's capture flags from the last frame).
    [[nodiscard]] auto inputCapture() const -> InputCapture override;
    /// @brief The viewport's size while it is open, through the world's main camera.
    [[nodiscard]] auto renderView() const -> std::optional<RenderView> override;
    void onShutdown() override;

    [[nodiscard]] auto context() -> EditorContext& { return m_context; }

private:
    enum class GizmoMode
    {
        None,
        Translate,
        Rotate,
        Scale,
    };

    void drawMenuBar();
    void drawViewport(FrameContext& frame);
    void updateGizmoMode();
    void drawGizmo(world::World& world);

    EditorContext m_context;
    std::vector<std::unique_ptr<Panel>> m_panels;
    std::string m_layoutFile; // UTF-8; ImGui reads and writes the layout there. Empty keeps none.
    GizmoMode m_gizmoMode{ GizmoMode::None };
    bool m_snapping{ false };
    bool m_viewportOpen{ false };
    std::optional<rhi::Extent2D> m_viewportExtent; // The view the viewport asks for; none while it is closed
};
}
