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
///        creates, opens and saves scene files, and reopens the project's recent ones (kept in
///        `<project>/.aegis/editor/recent_scenes.json`); the window title and the menu bar name the scene and mark unsaved changes, and
///        closing the window, New or Open ask about them first. A save replaces the file atomically. While the scene has
///        unsaved changes, the edit world is autosaved every few minutes (a preference) and on entering Play, to
///        `<project>/.aegis/editor/autosave/` (never the scene's file); Save, Save As and Don't Save delete the autosave.
///        At startup and when a scene opens, a bar above the viewport offers an autosave newer than the scene's file:
///        Restore opens it as the scene, unsaved; Discard deletes it; an edit hides the bar. The window layout is kept in
///        `<project>/.aegis/editor/imgui.ini` (none, like the autosaves, when headless or without a project). Log messages fill the
///        console next to stderr, and the user's preferences are read at startup and saved when they change
///        (`<config>/Aegis/editor.json`, none when headless). `--editor-panels all` opens every docked panel at startup (not the ImGui demo),
///        `--editor-select <name>` selects the first entity of that name, `--editor-play` enters play after the first frame
///        and `--editor-fold` starts with the right column folded and the Inspector in the flyout.
///        The panels start docked in the layout of docs/editor.md (View > Reset Layout restores it), in the look of
///        `ui/theme.h` scaled by the monitor's content scale and the UI-scale preference; the menus and shortcuts
///        come from one ActionRegistry. The drawer panels (Assets, Console, the debug panels) open in the drawer, a
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

    /// @brief The frame's UI: the menus, the panels, the viewport and the popups, timed as "Editor UI" in the profiler.
    void drawUi(FrameContext& frame);
    /// @brief Runs 'action' now, or once the user saved or discarded the unsaved changes.
    void request(PendingAction action, FrameContext& frame);
    void run(PendingAction action, FrameContext& frame);
    /// @brief Writes the edit world to the open scene file, or opens Save As without one. Whether it was written.
    auto save(FrameContext& frame) -> bool;
    /// @brief Writes the edit world to a new file at 'typed' in the project's content folder and opens that.
    auto saveAs(FrameContext& frame, std::string_view typed) -> std::expected<void, std::string>;
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
    /// @brief The panel called 'name'; null if there is none.
    [[nodiscard]] auto findPanel(std::string_view name) const -> Panel*;
    /// @brief Opens the panel called 'name', if there is one.
    void openPanel(std::string_view name);
    /// @brief Opens the drawer panel 'name': the drawer shows it, or, pinned, its window gets the focus.
    void showDrawer(std::string_view name);
    /// @brief Like showDrawer, but closes the drawer when it already shows 'name' (the status bar's buttons, the chords).
    void toggleDrawer(std::string_view name);
    /// @brief Gives the viewport the whole window, or the layout back (F11, the menu bar's button).
    void toggleFullViewport();
    /// @brief Makes the viewport follow m_fullViewport after it changed; entering closes the drawer.
    void syncFullViewport();
    /// @brief Esc leaves full viewport, once the drawer and the popups are closed and while the game doesn't have the keyboard.
    void leaveFullViewportOnEscape(bool drawerWasShown);
    /// @brief Full viewport's top bar: the play controls and the button that leaves it, with its shortcut.
    void drawFullViewportBar(FrameContext& frame);
    /// @brief Folds the right column into the side rail, or unfolds it (Ctrl+B, the menu bar's button).
    void toggleFold();
    /// @brief Whether the window 'title' is docked in the right column while it is folded: its contents go in the flyout.
    [[nodiscard]] auto inFoldedColumn(std::string_view title) const -> bool;
    /// @brief The column panel whose window is titled 'title'; null if there is none.
    [[nodiscard]] auto findColumnPanel(std::string_view title) const -> Panel*;
    /// @brief A rail button: shows the panel in the flyout, or closes it; a panel outside the folded column (floating,
    ///        docked elsewhere) gets the focus instead.
    void toggleFlyout(std::string_view title);
    /// @brief The rail at the right edge while folded: a button per column panel, lit while the flyout shows it.
    void drawRail();
    /// @brief The flyout over the viewport's right edge, as wide as the column, with the panel the rail shows, and its
    ///        closing rules: Esc (once the drawer is closed), its rail button, and for the Hierarchy a click into the
    ///        viewport. Losing the focus doesn't close it.
    void drawFlyout(FrameContext& frame, bool drawerWasShown);
    /// @brief Docks the drawer panel 'name' into the layout, and returns it to the drawer.
    void pin(std::string_view name);
    void unpin(std::string_view name);
    /// @brief The drawer over the bottom of the viewport, with a tab per open drawer panel that is not pinned, and its
    ///        closing rules: Esc, a click into the viewport, and a drag that leaves it (its drop still lands).
    void drawDrawer(FrameContext& frame);
    /// @brief The drawer's top edge, which a drag moves (EditorPreferences::drawerHeight).
    void drawDrawerResize(const Viewport::Rect& viewport, float top);
    /// @brief A column panel's window (the Hierarchy, the Inspector) with its contents.
    void drawColumnPanel(Panel& panel, FrameContext& frame);
    /// @brief A pinned drawer panel's docked window, with the pin button over the right end of its first row.
    void drawPinned(Panel& panel, FrameContext& frame);
    /// @brief The node a pinned panel docks into: where it was docked last, else the other pinned panels' node, else a
    ///        new one split off below the viewport.
    [[nodiscard]] auto pinNode(std::string_view name) -> std::uint32_t;
    /// @brief Docks the panels queued by pin (and pinned ones found undocked) before the dockspace updates.
    void dockPinned();
    /// @brief The asset still being dragged after the drawer it came from closed, as the drag's tooltip.
    void drawDragTooltip(FrameContext& frame);
    /// @brief Keeps the pinned panels in the layout file, next to the dock nodes they are docked in.
    void addSettingsHandler();
    /// @brief Rebuilds the style when the monitor's content scale or the UI-scale preference changed.
    void updateStyle();
    /// @brief The dockspace between the menu bar and the status bar; builds the default layout when there is none.
    void drawDockspace();
    void resetLayout();
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
    /// @brief Runs the action 'id' if it can run now.
    void runAction(std::string_view id, FrameContext& frame);
    void startPlay(FrameContext& frame);
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
    /// @brief The Assets and Console buttons, each its icon and name, lit while the drawer shows them.
    void drawDrawerButtons();
    /// @brief The Console button: its icon and name, then the console's warning and error counts while there are any, the
    ///        last problem in its tooltip.
    void drawConsoleButton();
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
    /// @brief The autosave of the scene file 'scene' (none for an untitled scene); empty without an autosave folder.
    [[nodiscard]] auto autosaveFile(std::optional<assets::AssetId> scene) const -> std::filesystem::path;
    /// @brief Autosaves the edit world when it has had unsaved changes for the preference's minutes since they began
    ///        or since the last autosave.
    void updateAutosave(FrameContext& frame);
    /// @brief Writes the edit world to the open scene's autosave (unless autosaving is off).
    void autosave(FrameContext& frame);
    /// @brief Deletes the autosave of 'scene' (its changes were saved or discarded), and the bar that offers it.
    void deleteAutosave(std::optional<assets::AssetId> scene);
    /// @brief Shows the recovery bar when the open scene has an autosave newer than its file; hides it otherwise.
    void offerRecovery(const assets::AssetDatabase& database);
    /// @brief The recovery bar's text, Restore and Discard; the viewport draws it above its image.
    void drawRecoveryBar();
    /// @brief Opens the offered autosave as the open scene: its file and ID stay, it is unsaved, its history empty.
    void restoreAutosave(FrameContext& frame);

    EditorContext m_context;
    log::SinkId m_logSink{};
    std::filesystem::path m_preferencesFile; // Empty keeps none
    EditorPreferences m_savedPreferences;
    std::vector<std::unique_ptr<Panel>> m_panels;
    std::unique_ptr<Panel> m_preferencesWindow; // Floats; Edit > Preferences... opens it
    std::unique_ptr<Panel> m_shortcutsWindow;   // Float; the Help menu opens them
    std::unique_ptr<Panel> m_aboutWindow;
    std::vector<Panel*> m_defaultOpen; // The panels open at startup; Reset Layout opens them again
    ActionRegistry m_actions;
    float m_contentScale{ 0.0f }; // As of the last style
    float m_uiScale{ 0.0f };
    bool m_resetLayout{ false };
    std::uint32_t m_dockspace{ 0 }; // The dockspace's ImGui ID, once drawn
    Drawers m_drawers;
    FullViewport m_fullViewport;
    SideRail m_rail;
    bool m_focusFlyout{ false };                // The flyout takes the focus next frame
    std::uint32_t m_pinNode{ 0 };               // The node the last pinned panel was docked into; 0 for none
    std::vector<std::string> m_dockRequests;    // Pinned panels to dock before the next dockspace update
    std::string m_focusPinned;                  // The pinned panel whose window takes the focus next frame
    bool m_focusDrawer{ false };                // The drawer takes the focus next frame
    std::optional<Viewport::Rect> m_drawerRect; // As of the last frame; none while closed
    bool m_dragFromDrawer{ false };             // The drag going on began in the drawer
    bool m_dragHidDrawer{ false };              // ... and left it, which closed it
    bool m_styleEditor{ false };                // ImGui's style editor window, in the Debug menu
    std::string m_layoutFile;                   // UTF-8; ImGui reads and writes the layout there. Empty keeps none.
    std::filesystem::path m_recentFile;         // Next to the layout; empty keeps none
    RecentScenes m_recent;
    Viewport m_viewport;
    Engine* m_engine{ nullptr };
    std::optional<assets::AssetId> m_scene; // The open scene file; none for a new scene
    std::optional<PendingAction> m_pending; // Waits for the unsaved-changes prompt
    bool m_openPrompt{ false };             // Open the prompt, the Open or the Save As popup in this frame
    bool m_openOpen{ false };
    bool m_openSaveAs{ false };
    std::string m_openSearch;                       // What the Open popup lists
    std::optional<assets::AssetId> m_openSelection; // Its selected scene
    std::string m_saveAsPath;                       // What Save As shows
    std::string m_fileError;                        // Why the last Save As failed, in its popup
    std::string m_title;                            // The window's, as last set
    // The autosave the recovery bar offers
    struct Recovery
    {
        std::filesystem::path file;
        std::string label; // What the bar says
    };
    enum class RecoveryChoice
    {
        None,
        Restore,
        Discard,
    };
    std::filesystem::path m_autosaveFolder;                              // Empty keeps none (headless, no project)
    std::optional<std::chrono::steady_clock::time_point> m_autosaveFrom; // When the unsaved changes began, or the last autosave
    std::optional<Recovery> m_recovery;                                  // While the bar shows
    RecoveryChoice m_recoveryChoice{ RecoveryChoice::None };             // Clicked in the bar, run once the panels are drawn
    PlaySession m_play;
    world::World* m_playWorld{ nullptr }; // While a play session runs; the engine owns it
    bool m_playAfterFirstFrame{ false };  // --editor-play
    bool m_foldOnFirstFrame{ false };     // --editor-fold
    RollingAverage<20> m_gpuMs;           // The frame graph's GPU time, which varies from frame to frame
};
}
