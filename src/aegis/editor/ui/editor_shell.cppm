export module aegis.editor:editor_shell;
import std;
import aegis.core;
import aegis.editor.core;
import aegis.engine;
import aegis.world;
import :actions;
import :panel;
import :viewport;
import :workspace;

namespace aegis::editor
{
class StatisticsCheck; // :statistics
}

export namespace aegis::editor
{
/// @brief The editor flavor's shell: the world stays in edit mode (no scripts run), and the editor's panels
///        read and edit it at the frame's safe point. Play runs a copy of it in a world of its own (the play world),
///        which the panels then show and edit, until Stop discards it; the UI is tinted meanwhile. The File menu
///        creates, opens and saves scene files, and reopens the project's recent ones (kept in
///        `<project>/.aegis/editor/recent_scenes.json`); the window title and the menu bar name the scene and mark unsaved changes, and
///        closing the window, New or Open ask about them first. A save replaces the file atomically. While the scene has
///        unsaved changes, the edit world is autosaved every few minutes (a preference) and on entering Play, to
///        `<project>/.aegis/editor/autosave/` (never the scene's file); Save, Save As and Don't Save delete the autosave.
///        At startup and when a scene opens, a bar above the viewport offers an autosave newer than the scene's file:
///        Restore opens it as the scene, unsaved; Discard deletes it; an edit hides the bar. The window layout is kept in
///        `<project>/.aegis/editor/imgui.ini` (none, like the autosaves, when headless or without a project). Log messages fill the
///        console next to stderr, and the user's preferences are read at startup and saved when they change
///        (`<config>/Aegis/editor.json`, none when headless). `--editor-panels all` opens every panel at startup but the floating ones,
///        `--editor-select <name>` selects the first entity of that name, `--editor-play` clicks Play after the first frame,
///        `--editor-stop <n>` clicks Stop once play has run n frames (and logs an error if it never stopped)
///        and `--editor-fold` starts with the right column folded and the Inspector in the flyout.
///        The panels start docked in the layout of docs/editor.md (View > Reset Layout restores it), in the look of
///        `ui/theme.h` scaled by the monitor's content scale and the UI-scale preference; the menus and shortcuts
///        come from one ActionRegistry. The drawer panels (Content, Console, the debug panels) open in the drawer, a
///        window over the bottom of the viewport, from the status bar's buttons or their chords, or dock below the
///        viewport while pinned; `--editor-panels all` pins them. Full viewport (F11) gives the viewport the whole
///        window, with play controls in a bar at its top; the panels stay in the layout below it, and
///        `--editor-full-viewport` starts in it. Ctrl+B folds the right column into the side rail, whose buttons open
///        the Hierarchy and the Inspector in a flyout over the viewport's right edge; the layout file keeps it folded.
class EditorShell final : public Shell
{
public:
    EditorShell();
    EditorShell(const EditorShell&) = delete;
    EditorShell(EditorShell&&) = delete;
    ~EditorShell() override;

    auto operator=(const EditorShell&) -> EditorShell& = delete;
    auto operator=(EditorShell&&) -> EditorShell& = delete;

    /// @brief Reads the editor's options (parseOptions) and fails on a bad one.
    auto onStartup(Engine& engine) -> std::expected<void, EngineError> override;
    /// @brief The editor's UI and edits; the main world updates in edit mode (no script runs).
    void onFrame(FrameContext& frame) override;
    /// @brief The input the editor's UI or the viewport's camera uses (as of the last frame); the game doesn't see it.
    ///        While the game has the input (the focused viewport, in play), only an editor chord's key until it is released.
    [[nodiscard]] auto inputCapture() const -> InputCapture override;
    /// @brief The viewport's view while it is open: the editor camera at the panel's settled size.
    [[nodiscard]] auto renderViews() const -> std::vector<RenderView> override;
    /// @brief With unsaved changes, asks whether to save them first and keeps the window open until answered.
    [[nodiscard]] auto canClose() -> bool override;
    void onShutdown() override;

    [[nodiscard]] auto context() -> EditorContext& { return m_context; }

    /// @brief The scene file the launcher loaded into the main world before run(): the document's scene at startup.
    ///        None (not called) starts with a new scene.
    void setStartupScene(assets::AssetId scene) { m_startupScene = scene; }

private:
    /// @brief The frame's UI: the menus, the panels, the viewport and the popups, timed as "Editor UI" in the profiler.
    void drawUi(FrameContext& frame);
    /// @brief Performs the actions the scene document asks for (SceneDocument::takeAction), once the panels are drawn.
    void runSceneActions(FrameContext& frame);
    void run(const SceneAction& action, FrameContext& frame);
    /// @brief The document's scene (none for a new scene) as an asset ID.
    [[nodiscard]] auto currentScene() const -> std::optional<assets::AssetId>;
    /// @brief Writes the edit world to the open scene file, or opens Save As without one (SceneDocument::save).
    void save(FrameContext& frame);
    /// @brief Writes the edit world to a new file at 'typed' in the project's content folder and opens that.
    auto saveAs(FrameContext& frame, std::string_view typed) -> std::expected<void, Error>;
    void drawFileMenu(FrameContext& frame);
    /// @brief The project's recent scenes, the most recent first; one that is gone is dropped from the list.
    void drawOpenRecentMenu(FrameContext& frame);
    /// @brief Puts the open scene first in the recent scenes, if it is a file of the project.
    void rememberScene(const assets::AssetDatabase& database);
    void saveRecentScenes();
    void drawOpenPopup(FrameContext& frame);
    void drawSaveAsPopup(FrameContext& frame);
    void drawUnsavedPrompt(FrameContext& frame);
    /// @brief The open scene's file name ("Untitled" for a new scene).
    [[nodiscard]] auto sceneFileName() const -> std::string;
    /// @brief sceneFileName, with a '*' while the scene has unsaved changes.
    [[nodiscard]] auto sceneName() const -> std::string;
    /// @brief The window title: the scene's name and the editor's.
    void updateTitle();
    void buildActions();
    /// @brief Full viewport's top bar: the play controls and the button that leaves it, with its shortcut.
    void drawFullViewportBar(FrameContext& frame);
    /// @brief Rebuilds the style when the monitor's content scale or the UI-scale preference changed.
    void updateStyle();
    /// @brief The Debug menu's ImGui windows: the demo and the style editor, while open.
    void drawStyleEditor();
    void drawMenuBar(FrameContext& frame);
    /// @brief "Undo Move 'Helmet'": 'verb' and the title of the entry it would take (its edits and group title), or
    ///        'verb' alone without one.
    [[nodiscard]] auto editLabel(std::string_view verb, std::span<const Edit> edits, std::string_view title) const -> std::string;
    /// @brief The play controls in the middle of the menu bar.
    void drawPlayControls(FrameContext& frame);
    /// @brief Play or Stop, Pause and Step, and "Playing" or "Paused" while a session runs.
    void drawPlayButtons(FrameContext& frame);
    /// @brief The fold and full viewport buttons and the scene's name left of them at the right of the menu bar, each when
    ///        it fits.
    void drawSceneName(FrameContext& frame);
    /// @brief Runs the action 'id' if it can run now; a deferred one once the panels have drawn (ActionRegistry::run).
    void runAction(std::string_view id, FrameContext& frame);
    /// @brief Creates the play world. Not while the panels draw: play.toggle is deferred.
    void startPlay(FrameContext& frame);
    /// @brief Destroys the play world. Not while the panels draw: play.toggle is deferred.
    void stopPlay(FrameContext& frame);
    /// @brief Selects the first entity created with that Name, or warns that there is none (--editor-select).
    void selectByName(std::string_view name);
    /// @brief Asks for this frame's updates: the edit world in edit mode, or the play world.
    void updateWorlds(FrameContext& frame);
    /// @brief The drawer buttons and the selection at the left, the keys for what the cursor is over in the middle, the
    ///        average CPU and GPU times and the frame rate at the right.
    void drawStatusBar(FrameContext& frame);
    /// @brief The CPU time, the GPU time and the frame rate, ending at 'right' on the bar's line, each in a slot of the
    ///        width its widest usual value takes, so neither they nor what is laid out left of them move as they change.
    ///        Returns where they start.
    auto drawTimings(FrameContext& frame, float right) -> float;
    /// @brief The active entity's name ("No selection" without one), and how many more are selected.
    [[nodiscard]] auto selectionLabel() const -> std::string;
    /// @brief A button per panel that asks for one (Panel::statusButton: Content, Console): its icon, name and counts, lit
    ///        while the drawer shows it.
    void drawPanelButtons();
    // A key and what it does there, for the status bar.
    struct KeyHint
    {
        std::string keys;
        std::string_view does;
    };
    /// @brief The keys for what the cursor is over: the viewport's camera and gizmo, flying, the hierarchy's rows.
    [[nodiscard]] auto keyHints() const -> std::vector<KeyHint>;
    /// @brief 'hints' between 'left' and 'right' (window x), centered; none when they don't fit.
    static void drawKeyHints(const std::vector<KeyHint>& hints, float left, float right);
    void savePreferencesIfChanged();
    /// @brief Writes the edit world to the open scene's autosave (SceneDocument::autosave); a failure is logged.
    void autosave(FrameContext& frame);
    /// @brief Shows the recovery bar when the open scene has an autosave newer than its file; hides it otherwise.
    void offerRecovery(const assets::AssetDatabase& database);
    /// @brief The recovery bar's text, Restore and Discard; the viewport draws it above its image.
    void drawRecoveryBar();
    /// @brief Opens the autosave 'file' as the open scene: its file and ID stay, it is unsaved, its history empty.
    void restoreAutosave(const std::filesystem::path& file, FrameContext& frame);

    EditorContext m_context;
    log::SinkId m_logSink{};
    std::filesystem::path m_preferencesFile; // Empty keeps none
    EditorPreferences m_savedPreferences;
    ActionRegistry m_actions;
    float m_contentScale{ 0.0f }; // As of the last style
    float m_uiScale{ 0.0f };
    bool m_styleEditor{ false };        // ImGui's style editor window, in the Debug menu
    bool m_demoWindow{ false };         // ImGui's demo window, in the Debug menu
    std::string m_layoutFile;           // UTF-8; ImGui reads and writes the layout there. Empty keeps none.
    std::filesystem::path m_recentFile; // Next to the layout; empty keeps none
    RecentScenes m_recent;
    Viewport m_viewport;
    Workspace m_workspace{ m_context, m_viewport }; // After the viewport it places
    Engine* m_engine{ nullptr };
    SceneDocument m_document{ m_context };
    std::optional<assets::AssetId> m_startupScene;  // setStartupScene's
    bool m_openOpen{ false };                       // Open the Open popup in this frame
    std::string m_openSearch;                       // What the Open popup lists
    std::optional<assets::AssetId> m_openSelection; // Its selected scene
    std::string m_saveAsPath;                       // What Save As shows
    std::string m_fileError;                        // Why the last Save As failed, in its popup
    std::string m_title;                            // The window's, as last set
    PlaySession m_play;
    world::World* m_playWorld{ nullptr };               // While a play session runs; the engine owns it
    bool m_playAfterFirstFrame{ false };                // --editor-play
    std::optional<int> m_stopAfter;                     // --editor-stop
    int m_playedFrames{ 0 };                            // Of the play session, for --editor-stop
    int m_stops{ 0 };                                   // Play sessions stopped, for --editor-stop
    bool m_clickPlay{ false };                          // drawPlayButtons clicks Play or Stop at its next draw
    bool m_drawing{ false };                            // While drawUi draws the panels: no world may be swapped
    bool m_foldOnFirstFrame{ false };                   // --editor-fold
    std::unique_ptr<StatisticsCheck> m_statisticsCheck; // --editor-check-stats
    RollingAverage<20> m_gpuMs;                         // The frame graph's GPU time, which varies from frame to frame
};
}
