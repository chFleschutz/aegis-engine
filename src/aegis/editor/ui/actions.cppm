export module aegis.editor:actions;
import std;
import aegis.editor.core;
import aegis.engine;

export namespace aegis::editor
{
/// @brief One thing the user can do, from a menu or a shortcut.
struct Action
{
    /// @brief Names it, like "file.save".
    std::string id;
    /// @brief What menus and tooltips call it.
    std::string label;
    /// @brief The shortcut menus show; none for an action that has no key.
    std::optional<Chord> chord;
    /// @brief A second shortcut that menus don't show (Redo's Ctrl+Shift+Z).
    std::optional<Chord> alternate;
    /// @brief Whether holding the keys runs it again and again (Undo).
    bool repeats{ false };
    /// @brief Whether it can run now; empty means always.
    std::function<bool()> enabled;
    std::function<void(FrameContext& frame)> run;
};

/// @brief The editor's actions, read by the menus and the shortcut handler, so a chord is written once and
///        shown as it is bound. The viewport's own keys (Q/W/E/R, F, WASD) are not actions: they depend on its focus.
class ActionRegistry
{
public:
    /// @brief Adds 'action'. Its id and chords must not be taken yet.
    void add(Action action);

    [[nodiscard]] auto find(std::string_view id) const -> const Action*;
    /// @brief Every action, in the order they were added (the shortcut table's).
    [[nodiscard]] auto actions() const -> std::span<const Action> { return m_actions; }
    /// @brief The action's chord as menus show it ("Ctrl+S"); empty if it has none.
    [[nodiscard]] auto shortcut(std::string_view id) const -> std::string;
    /// @brief "Label (Ctrl+S)", for a tooltip.
    [[nodiscard]] auto tooltip(std::string_view id) const -> std::string;

    /// @brief A menu item for the action: its label (or 'label') and shortcut, disabled while it can't run; a click runs it.
    void menuItem(std::string_view id, FrameContext& frame, std::string_view label = {}) const;
    /// @brief Runs the action whose chord was pressed this frame. Not while a text box has the keyboard (it has its own
    ///        undo). While 'gameHasKeyboard' only chords with a modifier run (hasModifier): plain keys are the game's.
    void handleShortcuts(FrameContext& frame, bool gameHasKeyboard = false);
    /// @brief Whether the key of the last chord pressed for an action, enabled or not, is still held: the game
    ///        doesn't see it until it is released.
    [[nodiscard]] auto chordHeld() const -> bool;

private:
    std::vector<Action> m_actions;
    std::optional<Key> m_heldKey; // The last chord's, until released
};
}
