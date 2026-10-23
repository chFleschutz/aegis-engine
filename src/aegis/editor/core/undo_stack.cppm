export module aegis.editor.core:undo_stack;
import std;
import aegis.reflect;
import aegis.world;

export namespace aegis::editor
{
/// @brief One edited field, recorded through the type registry: nothing here is editor- or game-specific.
struct FieldEdit
{
    StableId entity;
    std::string type;                       ///< The registered component or script type
    std::string field;                      ///< The field's name
    std::optional<std::size_t> scriptIndex; ///< Which of the entity's scripts, for a script type
    reflect::Value oldValue;
    reflect::Value newValue;
    bool open{ false }; ///< A continuous edit (a drag) that the next edit of the same field extends

    [[nodiscard]] auto sameTarget(const FieldEdit& other) const -> bool
    {
        return entity == other.entity && type == other.type && field == other.field && scriptIndex == other.scriptIndex;
    }
};

/// @brief The history of field edits: undo and redo walk it, a new edit drops what was undone, and a continuous edit
///        (one drag) is one entry. Pure data; applyEdit writes an entry to the world.
class UndoStack
{
public:
    /// @brief The most entries kept; the oldest are dropped past it.
    static constexpr std::size_t maxDepth = 1000;

    /// @brief Records an edit that is already applied. With 'continuous' (the widget is still being dragged or typed
    ///        in) the next edit of the same field extends this entry, keeping its old value, until seal() or an edit
    ///        that is not continuous. An edit that changes nothing is ignored.
    void record(FieldEdit edit, bool continuous = false);

    /// @brief Ends the continuous edit, if any: the next edit starts an entry of its own.
    void seal();

    [[nodiscard]] auto canUndo() const -> bool { return m_applied > 0; }
    [[nodiscard]] auto canRedo() const -> bool { return m_applied < m_edits.size(); }

    /// @brief Steps back and returns the edit to revert (write its oldValue), or null. Valid until the next change.
    [[nodiscard]] auto undo() -> const FieldEdit*;
    /// @brief Steps forward and returns the edit to reapply (write its newValue), or null. Valid until the next change.
    [[nodiscard]] auto redo() -> const FieldEdit*;

    /// @brief Marks the current state as the saved one (the scene was written).
    void markSaved();
    /// @brief Whether the state differs from the saved one: false right after markSaved, and again after undoing or
    ///        redoing back to it; always true once the saved state was dropped from the history.
    [[nodiscard]] auto dirty() const -> bool { return m_saved != m_applied; }

    /// @brief Forgets the history (another world); the current state counts as saved.
    void clear();

private:
    std::vector<FieldEdit> m_edits;
    std::size_t m_applied{ 0 };              ///< The entries in effect; the rest can be redone
    std::optional<std::size_t> m_saved{ 0 }; ///< m_applied when the scene was saved, or none once it left the history
};

/// @brief Which value of an edit to write.
enum class EditSide
{
    Old, ///< Undo
    New, ///< Redo
};

/// @brief The address of the entity's script number 'index' of type 'type', or null. Scripts are not in editor-core,
///        so the caller supplies this; components are found through their ComponentStorage.
using FindScript = std::function<void*(const reflect::TypeInfo& type, world::Registry& registry, world::Entity entity, std::size_t index)>;

/// @brief Writes one side of 'edit' through the type registry, as setField does (prepare, then the change hook).
/// @return UnknownType or UnknownField if the registry no longer has them, WrongCategory if the entity is gone or no
///         longer has the component or script; otherwise the setter's error
[[nodiscard]] auto applyEdit(const reflect::TypeRegistry& types, world::Registry& registry, const FieldEdit& edit, EditSide side,
    const std::function<void(const reflect::TypeInfo& type, void* object)>& prepare = {}, const FindScript& findScript = {}) -> std::expected<void, reflect::Error>;
}
