export module aegis.editor.core:undo_stack;
import std;
import :edits;

export namespace aegis::editor
{
/// @brief The history of edits: undo and redo walk it, a new edit drops what was undone, and a continuous edit (one
///        drag of a field or the gizmo, typing a name) is one entry. Pure data; applyEdit writes an entry to the world.
class UndoStack
{
public:
    /// @brief The most entries kept; the oldest are dropped past it.
    static constexpr std::size_t maxDepth = 1000;

    /// @brief Records an edit that is already applied. With 'continuous' (the widget is still being dragged or typed
    ///        in) the next edit of the same target extends this entry, keeping its old side, until seal() or an edit
    ///        that is not continuous; only field, component and rename edits merge. An edit that changes nothing is
    ///        ignored.
    void record(Edit edit, bool continuous = false);

    /// @brief Ends the continuous edit, if any: the next edit starts an entry of its own.
    void seal();

    [[nodiscard]] auto canUndo() const -> bool { return m_applied > 0; }
    [[nodiscard]] auto canRedo() const -> bool { return m_applied < m_edits.size(); }

    /// @brief The edit undo (or redo) would revert (or reapply) next, or null. Valid until the next change.
    [[nodiscard]] auto nextUndo() const -> const Edit* { return canUndo() ? &m_edits[m_applied - 1] : nullptr; }
    [[nodiscard]] auto nextRedo() const -> const Edit* { return canRedo() ? &m_edits[m_applied] : nullptr; }

    /// @brief Steps back and returns the edit to revert (write its old side), or null. Valid until the next change.
    [[nodiscard]] auto undo() -> const Edit*;
    /// @brief Steps forward and returns the edit to reapply (write its new side), or null. Valid until the next change.
    [[nodiscard]] auto redo() -> const Edit*;

    /// @brief Marks the current state as the saved one (the scene was written).
    void markSaved();
    /// @brief Whether the state differs from the saved one: false right after markSaved, and again after undoing or
    ///        redoing back to it; always true once the saved state was dropped from the history.
    [[nodiscard]] auto dirty() const -> bool { return m_saved != m_applied; }

    /// @brief Forgets the history (another world); the current state counts as saved.
    void clear();

private:
    std::vector<Edit> m_edits;
    std::size_t m_applied{ 0 };              ///< The entries in effect; the rest can be redone
    std::optional<std::size_t> m_saved{ 0 }; ///< m_applied when the scene was saved, or none once it left the history
};
}
