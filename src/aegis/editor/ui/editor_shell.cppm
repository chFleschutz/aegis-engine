export module aegis.editor:editor_shell;
import std;
import aegis.core;
import aegis.editor.core;
import aegis.engine;
import :panel;
import :viewport;

export namespace aegis::editor
{
/// @brief The editor flavor's shell: the world stays in edit mode (no scripts run), and the editor's panels
///        read and edit it at the frame's safe point. The window layout is kept in
///        `<project>/.aegis/editor/imgui.ini` (none when headless or without a project). Log messages fill the
///        console next to stderr, and the user's preferences are read at startup and saved when they change
///        (`<config>/Aegis/editor.json`, none when headless).
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
    /// @brief The editor's UI and edits; the main world updates in edit mode (no script runs).
    void onFrame(FrameContext& frame) override;
    /// @brief The input the editor's UI or the viewport's camera uses (as of the last frame); the game doesn't see it.
    [[nodiscard]] auto inputCapture() const -> InputCapture override;
    /// @brief The viewport's view while it is open: the editor camera at the panel's settled size.
    [[nodiscard]] auto renderView() const -> std::optional<RenderView> override;
    void onShutdown() override;

    [[nodiscard]] auto context() -> EditorContext& { return m_context; }

private:
    void drawMenuBar();
    void drawStatusBar(FrameContext& frame);
    void savePreferencesIfChanged();

    EditorContext m_context;
    log::SinkId m_logSink{};
    std::filesystem::path m_preferencesFile; // Empty keeps none
    EditorPreferences m_savedPreferences;
    std::vector<std::unique_ptr<Panel>> m_panels;
    std::string m_layoutFile; // UTF-8; ImGui reads and writes the layout there. Empty keeps none.
    Viewport m_viewport;
};
}
