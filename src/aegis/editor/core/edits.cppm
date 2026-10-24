export module aegis.editor.core:edits;
import std;
import aegis.reflect;
import aegis.serialize;
import aegis.world;
import :inspector;

export namespace aegis::editor
{
/// @brief A field's value, by the field's name.
struct FieldValue
{
    std::string field;
    reflect::Value value;
};

/// @brief The values of a component's or script's registered fields, in registration order.
using FieldValues = std::vector<FieldValue>;

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

/// @brief Every field of a component written at once: a gizmo drag writes the whole Transform.
struct ComponentEdit
{
    StableId entity;
    std::string type; ///< The registered component type
    FieldValues oldValues;
    FieldValues newValues;
    bool open{ false }; ///< A continuous edit (a drag) that the next edit of the same component extends

    [[nodiscard]] auto sameTarget(const ComponentEdit& other) const -> bool { return entity == other.entity && type == other.type; }
};

/// @brief A component added with its field values, or removed (then undo adds it back with them).
struct AddComponentEdit
{
    StableId entity;
    std::string type; ///< The registered component type
    FieldValues values;
    bool remove{ false };
};

/// @brief A script attached with its field values, or detached (then undo attaches it back at its index).
struct AttachScriptEdit
{
    StableId entity;
    std::string type;       ///< The registered script type
    std::size_t index{ 0 }; ///< Its place among the entity's scripts
    FieldValues values;
    bool detach{ false };
};

/// @brief An entity's Name changed.
struct RenameEdit
{
    StableId entity;
    std::string oldName;
    std::string newName;
    bool open{ false }; ///< Still being typed: the next rename of the same entity extends it

    [[nodiscard]] auto sameTarget(const RenameEdit& other) const -> bool { return entity == other.entity; }
};

/// @brief Where an entity sits in the hierarchy, and its local Transform there.
struct HierarchyPlace
{
    std::optional<StableId> parent; ///< None for a root entity
    std::size_t index{ 0 };         ///< Among the parent's children; unused for a root
    Transform transform;            ///< The local Transform that keeps the entity where it is in the world
};

/// @brief An entity reparented or reordered among its siblings, keeping its place in the world.
struct MoveEdit
{
    StableId entity;
    HierarchyPlace oldPlace;
    HierarchyPlace newPlace;
};

/// @brief An entity and its descendants created (Create, Duplicate) or destroyed, kept as a snapshot with their
///        IDs (serialize::saveScene of the root), so undoing a destroy restores the same StableIds.
struct SubtreeEdit
{
    serialize::Scene subtree;
    StableId root;
    std::optional<StableId> parent; ///< None for a root entity
    std::size_t index{ 0 };         ///< The root's place among the parent's children
    bool destroy{ false };
};

/// @brief One entry of the undo history: any edit the editor makes.
using Edit = std::variant<FieldEdit, ComponentEdit, AddComponentEdit, AttachScriptEdit, RenameEdit, MoveEdit, SubtreeEdit>;

/// @brief Which side of an edit to write.
enum class EditSide
{
    Old, ///< Undo
    New, ///< Redo
};

/// @brief The world edits apply to, with the type registry they are recorded through.
struct EditWorld
{
    const reflect::TypeRegistry& types;
    world::Registry& registry;
    PrepareField prepare{}; ///< Finishes a written or created component (the asset library resolves its references)
};

/// @brief What 'edit' does, for messages: "Health.value", "add Health", "destroy 'Lamp'".
[[nodiscard]] auto describe(const Edit& edit) -> std::string;

/// @brief The values of every registered field of 'object', an instance of 'type'.
[[nodiscard]] auto fieldValues(const reflect::TypeInfo& type, const void* object) -> FieldValues;

/// @brief Whether two values are the same; glm vectors compare per component.
[[nodiscard]] auto sameValue(const reflect::Value& a, const reflect::Value& b) -> bool;
[[nodiscard]] auto sameValues(const FieldValues& a, const FieldValues& b) -> bool;

/// @brief The entity's world transform composed from the local Transforms of it and its ancestors (no
///        GlobalTransform needed, which only the TransformSystem keeps up to date).
[[nodiscard]] auto worldTransform(world::Registry& registry, world::Entity entity) -> GlobalTransform;

/// @brief Destroys 'root' and its descendants, the deepest first (Registry::destroy only detaches the children).
void destroySubtree(world::Registry& registry, world::Entity root);

/// @brief Writes one side of 'edit' to the world through the type registry, with the component's change hook, as
///        the editor's own edits do.
/// @return Why it no longer applies (the entity, component or script is gone, a type or field is not registered,
///         a restored ID is taken); nothing is changed then, except the fields a component edit wrote before the
///         one that failed
[[nodiscard]] auto applyEdit(const EditWorld& world, const Edit& edit, EditSide side) -> std::expected<void, std::string>;
}
