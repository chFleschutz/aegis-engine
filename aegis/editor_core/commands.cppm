export module aegis.editor.core:commands;
import std;
import aegis.reflect;
import aegis.world;
import :edits;
import :error;
import :undo_stack;

// The editor's structural edits: each changes the world and records an edit on the undo stack, so undo and redo
// replay it through applyEdit. They refuse entities of a scene instance (serialize::InstanceMember): those are not
// saved, so the change would be lost. The field edits (editField, resetFields, pasteFields, TransformDrag) record the
// component fields of an entity of a scene instance as overrides of its holder instead (EntityAccess::Overridable).
export namespace aegis::editor
{
/// @brief How the editor may change an entity.
enum class EntityAccess
{
    Plain,       ///< An entity of the world's own scene: anything
    Overridable, ///< An entity of a scene instance: only its component fields, recorded as overrides of the holder
    Locked,      ///< An entity of an instance nested in another (or whose holder is gone): nothing
};

/// @brief How the editor may change 'entity' (valid).
[[nodiscard]] auto entityAccess(world::Registry& registry, world::Entity entity) -> EntityAccess;

/// @brief The fields of the component 'type' of 'entity' that its holder overrides; none unless it is Overridable.
[[nodiscard]] auto overriddenFields(world::Registry& registry, world::Entity entity, std::string_view type) -> std::vector<std::string>;

/// @brief Whether 'entity' holds a scene instance that unpackInstance takes: one that is not part of another instance.
[[nodiscard]] auto canUnpack(world::Registry& registry, world::Entity entity) -> bool;

/// @brief Creates an entity named 'name', the last child of 'parent' (a root entity for a null one), with a default
///        instance of each of 'components' (registered component types with storage). One undo entry: undo removes the entity with
///        its components, redo restores it.
/// @return The entity, or an error (and nothing changed) for a parent of a scene instance or a type that is not a
///         component with storage
[[nodiscard]] auto createEntity(const EditWorld& world, UndoStack& history, std::string name, world::Entity parent = {},
    std::span<const reflect::TypeInfo* const> components = {}) -> std::expected<world::Entity, Error>;

/// @brief Makes the entities of a scene or model instance under 'parent' (a root entity for a null one).
using CreateInstance = std::function<std::expected<world::Entity, Error>(world::Entity parent)>;

/// @brief Creates an instance through 'create' (the asset library's instantiateScene) under 'parent' and records it:
///        undo destroys the holder and its descendants, redo restores the holder with its StableId, and the holder's
///        SceneInstance recreates the instance's entities (through EditWorld::prepare).
/// @return The holder, or an error (and nothing changed) for a parent of a scene instance, a failed 'create' or an
///         instance that holds EditWorld::scene, the scene being edited, directly or through its instances (InstanceCycle)
[[nodiscard]] auto instantiateEntity(const EditWorld& world, UndoStack& history, world::Entity parent, const CreateInstance& create)
    -> std::expected<world::Entity, Error>;

/// @brief Destroys 'entity' and its descendants; undo restores them with their StableIds.
auto destroyEntity(const EditWorld& world, UndoStack& history, world::Entity entity) -> std::expected<void, Error>;

/// @brief Copies 'entity' and its descendants with new StableIds, right after it among its siblings.
/// @return The copy
[[nodiscard]] auto duplicateEntity(const EditWorld& world, UndoStack& history, world::Entity entity)
    -> std::expected<world::Entity, Error>;

/// @brief Gives the parameter 'parameter' of the material file 'material' (open in EditWorld::materials) the file value
///        'value', or removes it from the file for none, and shows it in the loaded material (EditWorld::applyMaterial).
///        With 'continuous' (a drag) the next edit of the same parameter extends this one.
/// @return MissingMaterial if the file is not open, or why the loaded material refused it (nothing changed then)
auto editMaterial(const EditWorld& world, UndoStack& history, std::uint64_t material, std::string_view parameter,
    std::optional<reflect::Node> value, bool continuous = false) -> std::expected<void, Error>;

/// @brief Renames 'entity'. With 'continuous' (the name is still being typed) the next rename extends this one.
auto renameEntity(const EditWorld& world, UndoStack& history, world::Entity entity, std::string name, bool continuous = false)
    -> std::expected<void, Error>;

/// @brief Makes 'entity' the child of 'parent' at 'index' (see Registry::insertChild), or a root entity for a null
///        'parent', keeping its place in the world: its local Transform is recomputed below the new parent.
/// @return An error (and nothing changed) for a cycle, or a parent of a scene instance
auto moveEntity(const EditWorld& world, UndoStack& history, world::Entity entity, world::Entity parent, std::size_t index)
    -> std::expected<void, Error>;

/// @brief Moves 'entity' next to 'sibling', before or after it under the sibling's parent (a drop between two rows of
///        the hierarchy), as moveEntity does.
auto moveEntityBeside(const EditWorld& world, UndoStack& history, world::Entity entity, world::Entity sibling, bool after)
    -> std::expected<void, Error>;

/// @brief Gives 'entity' a default instance of the component 'type' (see addComponent).
auto addComponent(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, Error>;

/// @brief Removes the entity's component 'type'; undo adds it back with its field values.
auto removeComponent(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, Error>;

/// @brief Sets every field of the entity's component 'type' (or, with 'scriptIndex', its script of that number and
///        type) to the values of a default instance. One undo entry; none if they already are the defaults.
auto resetFields(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type,
    std::optional<std::size_t> scriptIndex = std::nullopt) -> std::expected<void, Error>;

/// @brief Writes the fields that 'text' (componentToText of the same type, copied from any entity) holds to the
///        entity's component 'type' or script, as resetFields does; the other fields keep their values. One undo entry.
/// @return Why 'text' does not apply (see componentFromText); nothing is changed then
auto pasteFields(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type,
    std::optional<std::size_t> scriptIndex, std::string_view text) -> std::expected<void, Error>;

/// @brief Sets 'field' of the entity's component 'type' (or, with 'scriptIndex', its script of that number and type)
///        to 'value', running world.prepare and the change hook (setField). With 'continuous' (the widget is still
///        being dragged or typed in) the next edit of the same field extends this entry, until UndoStack::seal.
/// @return An error (nothing changed) for a missing component, an unknown field or a value the setter rejects
auto editField(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type,
    std::optional<std::size_t> scriptIndex, std::string_view field, const reflect::Value& value, bool continuous = false)
    -> std::expected<void, Error>;

/// @brief A drag of several entities' local Transforms (the gizmo moving the selected roots), recorded as one undo
///        entry when it ends. An entity of a scene instance is recorded as overrides of its holder; a Locked one is
///        left out.
class TransformDrag
{
public:
    /// @brief Begins a drag of 'entities', keeping their Transforms; those without one or Locked are left out. 'verb' (Move,
    ///        Rotate, Scale) titles the entry of several entities: "Move 3 entities".
    TransformDrag(const EditWorld& world, std::span<const world::Entity> entities, std::string verb);

    /// @brief Writes 'transform' to the local Transform of 'entity', one of the drag's, through its change hook (a
    ///        static entity only moves when told to). Records nothing.
    /// @return An error (nothing written) for an entity not in the drag or without a Transform, or a failed write
    auto update(const EditWorld& world, world::Entity entity, const Transform& transform) -> std::expected<void, Error>;

    /// @brief Records the drag as one undo entry: each entity's Transform from before it began to now. Entities that
    ///        are gone are skipped; a drag that ends where it began records nothing.
    void end(const EditWorld& world, UndoStack& history) const;

private:
    struct Start
    {
        StableId entity;
        FieldValues values; ///< Its Transform when the drag began
    };

    std::vector<Start> m_starts;
    std::string m_verb;
};

/// @brief Sets the overridden fields of the component 'type' of 'entity', an Overridable entity, back to the values of
///        its instance's scene and drops their overrides. One undo entry; none if the component has no overrides.
/// @return An error (nothing changed) for an entity that is not Overridable or a scene value that cannot be read
auto revertOverrides(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, Error>;

/// @brief Unpacks the scene instance 'holder' holds: the entities of its scene become plain entities of the world
///        with the IDs and values they have (the overrides are written to them already), and the holder loses its
///        SceneInstance. The instances those entities hold stay instances. Undo links them again.
/// @return An error (nothing changed) for an entity that holds no instance or is part of one (canUnpack)
auto unpackInstance(const EditWorld& world, UndoStack& history, world::Entity holder) -> std::expected<void, Error>;

/// @brief Attaches a new script of type 'type' to 'entity', after its other scripts.
auto attachScript(const EditWorld& world, UndoStack& history, world::Entity entity, const reflect::TypeInfo& type)
    -> std::expected<void, Error>;

/// @brief Detaches the entity's script number 'index'; undo attaches it back there with its field values.
auto detachScript(const EditWorld& world, UndoStack& history, world::Entity entity, std::size_t index)
    -> std::expected<void, Error>;
}
