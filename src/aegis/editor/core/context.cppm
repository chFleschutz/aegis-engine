export module aegis.editor.core:context;
import std;
import aegis.world;
import :console;
import :preferences;
import :selection;
import :undo_stack;

export namespace aegis::editor
{
/// @brief What the editor's panels share: the world they show and edit, and the selection. Panels never
///        call each other; they read and change this.
class EditorContext
{
public:
    /// @brief The world the panels show and edit, or null before the first frame.
    [[nodiscard]] auto world() const -> world::World* { return m_world; }
    /// @brief Switches to 'world'; a selection and an undo history made in another world are cleared.
    void setWorld(world::World& world);

    [[nodiscard]] auto selection() -> Selection& { return m_selection; }
    [[nodiscard]] auto selection() const -> const Selection& { return m_selection; }

    /// @brief The history of edits of the world; its saved marker is the scene's dirty flag.
    [[nodiscard]] auto undoStack() -> UndoStack& { return m_undo; }
    [[nodiscard]] auto undoStack() const -> const UndoStack& { return m_undo; }

    [[nodiscard]] auto console() -> ConsoleBuffer& { return m_console; }
    [[nodiscard]] auto console() const -> const ConsoleBuffer& { return m_console; }

    [[nodiscard]] auto preferences() -> EditorPreferences& { return m_preferences; }
    [[nodiscard]] auto preferences() const -> const EditorPreferences& { return m_preferences; }

    /// @brief Whether the scene has changes that are not saved (the status bar's marker).
    [[nodiscard]] auto unsaved() const -> bool { return m_unsaved; }
    void setUnsaved(bool unsaved) { m_unsaved = unsaved; }

    /// @brief The active selected entity, or null (nothing selected, no world, or the entity is gone).
    [[nodiscard]] auto selectedEntity() const -> world::Entity;
    /// @brief Whether 'entity' is selected.
    [[nodiscard]] auto isSelected(world::Entity entity) const -> bool;
    /// @brief Selects only 'entity', by its StableId; null or an entity of no world clears the selection.
    void select(world::Entity entity);

private:
    [[nodiscard]] auto idOf(world::Entity entity) const -> std::optional<StableId>;

    world::World* m_world{ nullptr };
    Selection m_selection;
    UndoStack m_undo;
    ConsoleBuffer m_console;
    EditorPreferences m_preferences;
    bool m_unsaved{ false };
};
}
