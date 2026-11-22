export module aegis.editor:workspace;
import std;
import aegis.editor.core;
import aegis.engine;
import :panel;
import :viewport;

namespace aegis::editor
{
/// @brief The status bar's window: a click into it leaves the drawer open, since its buttons switch it.
inline constexpr const char* statusBarWindow = "##StatusBar";
}

export namespace aegis::editor
{
/// @brief The editor's panels and where they sit: the dockspace between the menu bar and the status bar with the
///        default layout of docs/editor.md (Reset Layout restores it), the right column and the side rail it folds into
///        with its flyout, the drawer over the bottom of the viewport and the drawer panels pinned below it, and full
///        viewport. It begins and ends every panel's window by the panel's Placement; a panel draws only its contents.
///        The layout file keeps the docked windows (ImGui's own sections), the pinned panels and the folded column (the
///        "[Aegis]" sections of addSettingsHandler). Panels are named by name(), the rail and the flyout by their window
///        titles.
class Workspace
{
public:
    /// @brief The workspace of the panels that draw into 'editor', placed around 'viewport'; both must outlive it.
    Workspace(EditorContext& editor, Viewport& viewport);
    Workspace(const Workspace&) = delete;
    Workspace(Workspace&&) = delete;
    ~Workspace() = default;

    auto operator=(const Workspace&) -> Workspace& = delete;
    auto operator=(Workspace&&) -> Workspace& = delete;

    /// @brief Adds 'panel' after the others, in its menu's order; one that is open now opens again on Reset Layout.
    void add(std::unique_ptr<Panel> panel);
    /// @brief The panels, in the order they were added.
    [[nodiscard]] auto panels() const -> std::span<const std::unique_ptr<Panel>> { return m_panels; }
    /// @brief The panel called 'name'; null if there is none.
    [[nodiscard]] auto find(std::string_view name) const -> Panel*;
    /// @brief Opens the panel called 'name', if there is one.
    void open(std::string_view name);
    /// @brief `--editor-panels all`: opens every panel but the floating ones and pins the drawer panels; the first
    ///        one's tab shows.
    void openAll();

    /// @brief Keeps the pinned panels and the folded column in the layout file: "[Aegis][Drawers]" with the line
    ///        Drawers::settingsLine writes, "[Aegis][Rail]" with SideRail::settingsLine's. Call it before ImGui reads the
    ///        layout file (the first frame), and removeSettingsHandler before this workspace is gone.
    void addSettingsHandler();
    void removeSettingsHandler();

    /// @brief Which drawer panel the drawer shows and which ones are pinned.
    [[nodiscard]] auto drawers() const -> const Drawers& { return m_drawers; }
    /// @brief Opens the drawer panel 'name': the drawer shows it, or, pinned, its window gets the focus.
    void showDrawer(std::string_view name);
    /// @brief Like showDrawer, but closes the drawer when it already shows 'name' (the status bar's buttons, the chords).
    void toggleDrawer(std::string_view name);

    /// @brief Whether the right column is folded into the side rail.
    [[nodiscard]] auto folded() const -> bool { return m_rail.folded(); }
    /// @brief Folds the right column into the side rail, or unfolds it (Ctrl+B, the menu bar's button).
    void toggleFold();
    /// @brief Folds the right column and shows the column panel 'name' in the flyout (`--editor-fold`); call it once
    ///        ImGui read the layout file, which keeps the folded state too.
    void foldInto(std::string_view name);
    /// @brief Opens the panel 'name' and brings it into sight: full viewport ends, and the flyout shows it while it is
    ///        in the folded column (a rename needs the Inspector's name field).
    void reveal(std::string_view name);
    /// @brief Whether the cursor is over the window of the panel 'name', or over the flyout that shows it.
    [[nodiscard]] auto hovers(std::string_view name) const -> bool;

    /// @brief Whether the viewport has the whole window: the menu bar, the status bar and the floating windows are not drawn.
    [[nodiscard]] auto fullViewport() const -> bool { return m_fullViewport.active(); }
    /// @brief Gives the viewport the whole window, or the layout back (F11, the menu bar's button).
    void toggleFullViewport();
    /// @brief Play started: enters full viewport when 'onPlay' (the preference) is on; Stop leaves it if Play entered it.
    void play(bool onPlay);
    void stop();

    /// @brief The default layout again, with the panels open at startup (View > Reset Layout).
    void resetLayout();

    /// @brief The asset still being dragged after the drawer it came from closed, as the drag's tooltip. First in the
    ///        frame, so a drop target's own tooltip replaces it.
    void drawDragTooltip(FrameContext& frame);
    /// @brief The dockspace and the open panels' windows, after the menu bar and before the viewport. In full viewport
    ///        the docked panels are still drawn, below the viewport's window: a docked window that isn't submitted
    ///        leaves its node, and a node that loses its last window is deleted, so the layout would not come back.
    void drawLayout(FrameContext& frame);
    /// @brief The drawer and the flyout over the viewport, whose image places them, and Esc leaving full viewport;
    ///        after the viewport.
    void drawOverlays(FrameContext& frame);
    /// @brief The rail at the right edge while folded: a button per column panel, lit while the flyout shows it. Not
    ///        in full viewport.
    void drawRail();

private:
    /// @brief Makes the viewport follow m_fullViewport after it changed; entering closes the drawer.
    void syncFullViewport();
    /// @brief Esc leaves full viewport, once the drawer and the popups are closed and while the game doesn't have the keyboard.
    void leaveFullViewportOnEscape(bool drawerWasShown);
    /// @brief Whether the window 'title' is docked in the right column while it is folded: its contents go in the flyout.
    [[nodiscard]] auto inFoldedColumn(std::string_view title) const -> bool;
    /// @brief The column panel whose window is titled 'title'; null if there is none.
    [[nodiscard]] auto findColumnPanel(std::string_view title) const -> Panel*;
    /// @brief A rail button: shows the panel in the flyout, or closes it; a panel outside the folded column (floating,
    ///        docked elsewhere) gets the focus instead.
    void toggleFlyout(std::string_view title);
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
    /// @brief The dockspace between the menu bar and the status bar; builds the default layout when there is none.
    void drawDockspace();

    EditorContext& m_editor;
    Viewport& m_viewport;
    std::vector<std::unique_ptr<Panel>> m_panels;
    std::vector<Panel*> m_defaultOpen; // The panels open at startup; Reset Layout opens them again
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
};
}
