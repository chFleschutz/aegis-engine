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
    /// @brief Switches to 'world'; a selection and an undo history made in another world are cleared. Not while
    ///        playing (exitPlay first).
    void setWorld(world::World& world);

    /// @brief Switches the panels to 'play', a copy of the edit world: the selection stays (the copy keeps the
    ///        StableIds), the edit world's history is set aside and edits made while playing get one of their own.
    void enterPlay(world::World& play);
    /// @brief Back to the edit world from before enterPlay: the play history is dropped and the edit one restored.
    void exitPlay();
    /// @brief Whether the panels show a play world (enterPlay).
    [[nodiscard]] auto playing() const -> bool { return m_editWorld != nullptr; }
    /// @brief The world that is saved: the edit world, also while playing; null before the first frame.
    [[nodiscard]] auto editWorld() const -> world::World* { return m_editWorld ? m_editWorld : m_world; }

    [[nodiscard]] auto selection() -> Selection& { return m_selection; }
    [[nodiscard]] auto selection() const -> const Selection& { return m_selection; }

    /// @brief The history of edits of the world the panels show (while playing, the play world's); the edit world's
    ///        saved marker is the scene's dirty flag (unsaved).
    [[nodiscard]] auto undoStack() -> UndoStack& { return m_undo; }
    [[nodiscard]] auto undoStack() const -> const UndoStack& { return m_undo; }

    [[nodiscard]] auto console() -> ConsoleBuffer& { return m_console; }
    [[nodiscard]] auto console() const -> const ConsoleBuffer& { return m_console; }

    [[nodiscard]] auto preferences() -> EditorPreferences& { return m_preferences; }
    [[nodiscard]] auto preferences() const -> const EditorPreferences& { return m_preferences; }

    /// @brief Whether the edit world has changes that are not saved (the edit history's saved marker), also while playing.
    [[nodiscard]] auto unsaved() const -> bool { return playing() ? m_editUndo.dirty() : m_undo.dirty(); }

    /// @brief The active selected entity, or null (nothing selected, no world, or the entity is gone).
    [[nodiscard]] auto selectedEntity() const -> world::Entity;
    /// @brief Whether 'entity' is selected.
    [[nodiscard]] auto isSelected(world::Entity entity) const -> bool;
    /// @brief Selects only 'entity', by its StableId; null or an entity of no world clears the selection.
    void select(world::Entity entity);

private:
    [[nodiscard]] auto idOf(world::Entity entity) const -> std::optional<StableId>;

    world::World* m_world{ nullptr };
    world::World* m_editWorld{ nullptr }; // While playing; m_world is the play world then
    Selection m_selection;
    UndoStack m_undo;
    UndoStack m_editUndo; // The edit world's history while playing
    ConsoleBuffer m_console;
    EditorPreferences m_preferences;
};
}
