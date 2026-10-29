export module aegis.editor:editor_shell;
import std;
import aegis.core;
import aegis.editor.core;
import aegis.engine;
import aegis.world;
import :actions;
import :panel;
import :viewport;

export namespace aegis::editor
{
/// @brief The editor flavor's shell: the world stays in edit mode (no scripts run), and the editor's panels
///        read and edit it at the frame's safe point. Play runs a copy of it in a world of its own (the play world),
///        which the panels then show and edit, until Stop discards it; the UI is tinted meanwhile. The File menu
///        creates, opens and saves scene files; the window title and the menu bar name the scene and mark unsaved changes, and
///        closing the window, New or Open ask about them first. The window layout is kept in
///        `<project>/.aegis/editor/imgui.ini` (none when headless or without a project). Log messages fill the
///        console next to stderr, and the user's preferences are read at startup and saved when they change
///        (`<config>/Aegis/editor.json`, none when headless). `--editor-panels all` opens every docked panel at startup (not the ImGui demo).
///        The panels start docked in the layout of docs/editor.md (View > Reset Layout restores it), in the look of
///        `ui/theme.h` scaled by the monitor's content scale and the UI-scale preference; the menus and shortcuts
///        come from one ActionRegistry.
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
    ///        While the game has the input (the focused viewport, in play), only an editor chord's key until it is released.
    [[nodiscard]] auto inputCapture() const -> InputCapture override;
    /// @brief The viewport's view while it is open: the editor camera at the panel's settled size.
    [[nodiscard]] auto renderView() const -> std::optional<RenderView> override;
    /// @brief With unsaved changes, asks whether to save them first and keeps the window open until answered.
    [[nodiscard]] auto canClose() -> bool override;
    void onShutdown() override;

    [[nodiscard]] auto context() -> EditorContext& { return m_context; }

private:
    // What waits for the unsaved-changes prompt: it runs once the changes are saved or discarded.
    struct PendingAction
    {
        enum class Kind
        {
            New,
            Open,
            Close,
        };
        Kind kind;
        assets::AssetId scene{}; // Open's
    };

    /// @brief Runs 'action' now, or once the user saved or discarded the unsaved changes.
    void request(PendingAction action, FrameContext& frame);
    void run(PendingAction action, FrameContext& frame);
    /// @brief Writes the edit world to the open scene file, or opens Save As without one. Whether it was written.
    auto save(FrameContext& frame) -> bool;
    /// @brief Writes the edit world to a new file at 'typed' in the project's content folder and opens that.
    auto saveAs(FrameContext& frame, std::string_view typed) -> std::expected<void, std::string>;
    void drawFileMenu(FrameContext& frame);
    void drawOpenPopup(FrameContext& frame);
    void drawSaveAsPopup(FrameContext& frame);
    void drawUnsavedPrompt(FrameContext& frame);
    /// @brief The open scene's file name ("Untitled" for a new scene), with a '*' while it has unsaved changes.
    [[nodiscard]] auto sceneName() const -> std::string;
    /// @brief The window title: the scene's name and the editor's.
    void updateTitle();
    void buildActions();
    /// @brief Opens the panel called 'name', if there is one.
    void openPanel(std::string_view name);
    /// @brief Rebuilds the style when the monitor's content scale or the UI-scale preference changed.
    void updateStyle();
    /// @brief The dockspace between the menu bar and the status bar; builds the default layout when there is none.
    void drawDockspace();
    void resetLayout();
    void drawStyleEditor();
    void drawMenuBar(FrameContext& frame);
    void drawPlayControls(FrameContext& frame);
    /// @brief The scene's name at the right of the menu bar, when it fits.
    void drawSceneName();
    void startPlay(FrameContext& frame);
    void stopPlay(FrameContext& frame);
    /// @brief Asks for this frame's updates: the edit world in edit mode, or the play world.
    void updateWorlds(FrameContext& frame);
    /// @brief The selection at the left, the average frame and GPU times in the middle, the warning and error counts at the right.
    void drawStatusBar(FrameContext& frame);
    /// @brief The active entity's name ("No selection" without one), and how many more are selected.
    [[nodiscard]] auto selectionLabel() const -> std::string;
    /// @brief The console's warning and error counts, with the last one as their tooltip; a click shows them in the console.
    void drawProblemCounts();
    void savePreferencesIfChanged();

    EditorContext m_context;
    log::SinkId m_logSink{};
    std::filesystem::path m_preferencesFile; // Empty keeps none
    EditorPreferences m_savedPreferences;
    std::vector<std::unique_ptr<Panel>> m_panels;
    std::unique_ptr<Panel> m_preferencesWindow; // Floats; Edit > Preferences... opens it
    std::vector<Panel*> m_defaultOpen;          // The panels open at startup; Reset Layout opens them again
    ActionRegistry m_actions;
    float m_contentScale{ 0.0f }; // As of the last style
    float m_uiScale{ 0.0f };
    bool m_resetLayout{ false };
    bool m_styleEditor{ false }; // ImGui's style editor window, in the Debug menu
    std::string m_layoutFile;    // UTF-8; ImGui reads and writes the layout there. Empty keeps none.
    Viewport m_viewport;
    Engine* m_engine{ nullptr };
    std::optional<assets::AssetId> m_scene; // The open scene file; none for a new scene
    std::optional<PendingAction> m_pending; // Waits for the unsaved-changes prompt
    bool m_openPrompt{ false };             // Open the prompt, the Open or the Save As popup in this frame
    bool m_openOpen{ false };
    bool m_openSaveAs{ false };
    std::string m_saveAsPath; // What Save As shows
    std::string m_fileError;  // Why the last Save As failed, in its popup
    std::string m_title;      // The window's, as last set
    PlaySession m_play;
    world::World* m_playWorld{ nullptr }; // While a play session runs; the engine owns it
    RollingAverage<20> m_gpuMs;           // The frame graph's GPU time, which varies from frame to frame
};
}
