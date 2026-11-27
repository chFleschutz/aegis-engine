export module aegis.editor.core:context;
import std;
import aegis.world;
import :console;
import :material_document;
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

    /// @brief The material files opened in the Material panel, with their edits.
    [[nodiscard]] auto materials() -> MaterialDocuments& { return m_materials; }
    [[nodiscard]] auto materials() const -> const MaterialDocuments& { return m_materials; }

    /// @brief Whether there are changes that are not saved: the edit world's (the edit history's saved marker, also while
    ///        playing) or a material file's (MaterialDocuments::dirty), which Save writes too.
    [[nodiscard]] auto unsaved() const -> bool { return historyUnsaved() || m_materials.dirty(); }
    /// @brief Whether the edit history is away from its saved marker (also while playing): the world's edits, and the
    ///        material edits recorded there.
    [[nodiscard]] auto historyUnsaved() const -> bool { return playing() ? m_editUndo.dirty() : m_undo.dirty(); }
    /// @brief Marks the edit world as saved (its history's saved marker), also while playing.
    void markSaved() { (playing() ? m_editUndo : m_undo).markSaved(); }
    /// @brief Forgets the selection and the edit history, for another scene in the same world. Not while playing.
    void resetScene();
    /// @brief The scene asset the edit world is saved as (no instance in it may hold that scene); none for a new scene.
    [[nodiscard]] auto scene() const -> std::optional<std::uint64_t> { return m_scene; }
    /// @brief Names the scene asset the edit world is saved as (SceneDocument does, as it opens and saves scenes).
    void setScene(std::optional<std::uint64_t> scene) { m_scene = scene; }

    /// @brief Asks the shell to open the scene file 'scene' (an asset ID) once the panels are drawn: replacing the
    ///        world while they walk it would break them. A later request this frame replaces it.
    void requestOpenScene(std::uint64_t scene) { m_sceneRequest = scene; }
    /// @brief The requested scene, once; none if nothing was requested since.
    [[nodiscard]] auto takeSceneRequest() -> std::optional<std::uint64_t> { return std::exchange(m_sceneRequest, std::nullopt); }

    /// @brief Asks the Material panel to show the material file 'material' (an asset ID): a double click in the content
    ///        browser, or a new material. A later request this frame replaces it.
    void requestOpenMaterial(std::uint64_t material) { m_materialRequest = material; }
    /// @brief Whether a material was requested and not taken yet.
    [[nodiscard]] auto materialRequested() const -> bool { return m_materialRequest.has_value(); }
    /// @brief The requested material, once; none if nothing was requested since.
    [[nodiscard]] auto takeMaterialRequest() -> std::optional<std::uint64_t> { return std::exchange(m_materialRequest, std::nullopt); }

    /// @brief Asks the inspector to put the keyboard in the selected entity's name field (F2) at its next draw.
    void requestRename() { m_renameRequest = true; }
    /// @brief Whether a rename was requested and not taken yet.
    [[nodiscard]] auto renameRequested() const -> bool { return m_renameRequest; }
    /// @brief Whether a rename was requested since; once.
    [[nodiscard]] auto takeRenameRequest() -> bool { return std::exchange(m_renameRequest, false); }

    /// @brief Asks the viewport to frame the selected entity with its camera (F, a double click on a hierarchy row) at its next draw.
    void requestFocus() { m_focusRequest = true; }
    /// @brief Whether a focus was requested since; once.
    [[nodiscard]] auto takeFocusRequest() -> bool { return std::exchange(m_focusRequest, false); }

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
    MaterialDocuments m_materials;
    std::optional<std::uint64_t> m_scene;
    std::optional<std::uint64_t> m_sceneRequest;
    std::optional<std::uint64_t> m_materialRequest;
    bool m_renameRequest{ false };
    bool m_focusRequest{ false };
};
}
