export module aegis.editor.core:context;
import std;
import aegis.world;
import :selection;

export namespace aegis::editor
{
/// @brief What the editor's panels share: the world they show and edit, and the selection. Panels never
///        call each other; they read and change this.
class EditorContext
{
public:
    /// @brief The world the panels show and edit, or null before the first frame.
    [[nodiscard]] auto world() const -> world::World* { return m_world; }
    /// @brief Switches to 'world'; a selection made in another world is cleared.
    void setWorld(world::World& world);

    [[nodiscard]] auto selection() -> Selection& { return m_selection; }
    [[nodiscard]] auto selection() const -> const Selection& { return m_selection; }

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
};
}
