export module aegis.editor.core:commands;
import std;
import aegis.reflect;
import aegis.world;
import :edits;
import :undo_stack;

// The editor's structural edits: each changes the world and records an edit on the undo stack, so undo and redo
// replay it through applyEdit. They refuse entities of a scene instance (serialize::InstanceMember): those are not
// saved, so the change would be lost.
export namespace aegis::editor
{
/// @brief Creates an entity named 'name', the last child of 'parent' (a root entity for a null one).
[[nodiscard]] auto createEntity(const EditWorld& world, UndoStack& history, std::string name, world::Entity parent = {})
    -> std::expected<world::Entity, std::string>;

/// @brief Destroys 'entity' and its descendants; undo restores them with their StableIds.
auto destroyEntity(const EditWorld& world, UndoStack& history, world::Entity entity) -> std::expected<void, std::string>;

/// @brief Copies 'entity' and its descendants with new StableIds, right after it among its siblings.
/// @return The copy
[[nodiscard]] auto duplicateEntity(const EditWorld& world, UndoStack& history, world::Entity entity)
    -> std::expected<world::Entity, std::string>;

/// @brief Renames 'entity'. With 'continuous' (the name is still being typed) the next rename extends this one.
auto renameEntity(const EditWorld& world, UndoStack& history, world::Entity entity, std::string name, bool continuous = false)
    -> std::expected<void, std::string>;

/// @brief Makes 'entity' the child of 'parent' at 'index' (see Registry::insertChild), or a root entity for a null
///        'parent', keeping its place in the world: its local Transform is recomputed below the new parent.
/// @return An error (and nothing changed) for a cycle, or a parent of a scene instance
auto moveEntity(const EditWorld& world, UndoStack& history, world::Entity entity, world::Entity parent, std::size_t index)
    -> std::expected<void, std::string>;

/// @brief Moves 'entity' next to 'sibling', before or after it under the sibling's parent (a drop between two rows of
///        the hierarchy), as moveEntity does.
auto moveEntityBeside(const EditWorld& world, UndoStack& history, world::Entity entity, world::Entity sibling, bool after)
    -> std::expected<void, std::string>;

/// @brief Gives 'entity' a default instance of the component 'type' (see addComponent).
auto addComponent(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, std::string>;

/// @brief Removes the entity's component 'type'; undo adds it back with its field values.
auto removeComponent(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, std::string>;

/// @brief Attaches a new script of type 'type' to 'entity', after its other scripts.
auto attachScript(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, std::string>;

/// @brief Detaches the entity's script number 'index'; undo attaches it back there with its field values.
auto detachScript(const EditWorld& world, UndoStack& history, world::Entity entity, std::size_t index)
    -> std::expected<void, std::string>;
}
