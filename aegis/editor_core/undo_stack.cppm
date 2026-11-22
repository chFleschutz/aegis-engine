export module aegis.editor.core:undo_stack;
import std;
import :edits;

export namespace aegis::editor
{
/// @brief The history of edits: undo and redo walk it, a new edit drops what was undone, and a continuous edit (one
///        drag of a field or the gizmo, typing a name) is one entry. An entry holds one edit, or every edit recorded
///        while a group was open (beginGroup). Pure data; applyEdits writes an entry to the world.
class UndoStack
{
public:
    /// @brief The most entries kept; the oldest are dropped past it.
    static constexpr std::size_t maxDepth = 1000;

    /// @brief Records an edit that is already applied. With 'continuous' (the widget is still being dragged or typed
    ///        in) the next edit of the same target extends this one, keeping its old side, until seal() or an edit
    ///        that is not continuous; only field, component and rename edits merge, and only within one entry. An
    ///        edit that changes nothing is ignored. While a group is open the edit joins the group's entry.
    void record(Edit edit, bool continuous = false);

    /// @brief Ends the continuous edit, if any: the next edit starts an entry of its own.
    void seal();

    /// @brief Opens a group: what is recorded until the matching endGroup() is one entry, titled 'title' for the Edit
    ///        menu ("Delete 5 entities"). A group opened inside an open group joins it (its title is ignored). A group
    ///        that records nothing adds no entry. Prefer UndoGroup, which ends it.
    void beginGroup(std::string title);
    /// @brief Closes the group beginGroup() opened; the outermost one ends its entry, sealing its continuous edit.
    void endGroup();

    [[nodiscard]] auto canUndo() const -> bool { return m_applied > 0; }
    [[nodiscard]] auto canRedo() const -> bool { return m_applied < m_entries.size(); }

    /// @brief The edits of the entry undo (or redo) would revert (or reapply) next, in the order they were recorded;
    ///        empty if there is none. Valid until the next change.
    [[nodiscard]] auto nextUndo() const -> std::span<const Edit> { return canUndo() ? std::span<const Edit>{ m_entries[m_applied - 1].edits } : std::span<const Edit>{}; }
    [[nodiscard]] auto nextRedo() const -> std::span<const Edit> { return canRedo() ? std::span<const Edit>{ m_entries[m_applied].edits } : std::span<const Edit>{}; }
    /// @brief The title of the group that recorded that entry; empty for an entry of one edit recorded outside a group.
    [[nodiscard]] auto nextUndoTitle() const -> std::string_view { return canUndo() ? std::string_view{ m_entries[m_applied - 1].title } : std::string_view{}; }
    [[nodiscard]] auto nextRedoTitle() const -> std::string_view { return canRedo() ? std::string_view{ m_entries[m_applied].title } : std::string_view{}; }

    /// @brief Steps back and returns the entry's edits to revert (write their old sides, the last first), or none.
    ///        Valid until the next change.
    [[nodiscard]] auto undo() -> std::span<const Edit>;
    /// @brief Steps forward and returns the entry's edits to reapply (write their new sides, the first first), or
    ///        none. Valid until the next change.
    [[nodiscard]] auto redo() -> std::span<const Edit>;

    /// @brief Marks the current state as the saved one (the scene was written).
    void markSaved();
    /// @brief Marks the current state as unsaved, with the saved state outside the history (a scene restored from
    ///        its autosave): dirty until the next markSaved, whatever undo and redo do.
    void markUnsaved();
    /// @brief Whether the state differs from the saved one: false right after markSaved, and again after undoing or
    ///        redoing back to it; always true once the saved state was dropped from the history.
    [[nodiscard]] auto dirty() const -> bool { return m_saved != m_applied; }

    /// @brief Forgets the history (another world); the current state counts as saved.
    void clear();

private:
    /// @brief One step of the history
    struct Entry
    {
        std::vector<Edit> edits; ///< In the order they were recorded
        std::string title;       ///< The group's; empty for an edit recorded outside a group
    };

    void push(Entry entry);

    std::vector<Entry> m_entries;
    std::size_t m_applied{ 0 };              ///< The entries in effect; the rest can be redone
    std::optional<std::size_t> m_saved{ 0 }; ///< m_applied when the scene was saved, or none once it left the history
    std::size_t m_groupDepth{ 0 };           ///< The groups open
    std::string m_groupTitle;                ///< The outermost open group's
    bool m_groupRecorded{ false };           ///< The open group has its entry, the last one
};

/// @brief Keeps a group of the stack open for its lifetime (UndoStack::beginGroup): what it records is one entry.
class UndoGroup
{
public:
    UndoGroup(UndoStack& stack, std::string title) :
        m_stack{ stack }
    {
        m_stack.beginGroup(std::move(title));
    }
    ~UndoGroup() { m_stack.endGroup(); }

    UndoGroup(const UndoGroup&) = delete;
    UndoGroup(UndoGroup&&) = delete;
    auto operator=(const UndoGroup&) -> UndoGroup& = delete;
    auto operator=(UndoGroup&&) -> UndoGroup& = delete;

private:
    UndoStack& m_stack;
};
}
