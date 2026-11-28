export module aegis.serialize:scene;
import std;
import aegis.assets;
import aegis.reflect;
import aegis.world;
import :error;

// Scene files (docs/asset-system.md, "Scene files"):
//
//     {
//       "type": "Scene",
//       "id": "9d41c7e0a25b6f18",
//       "version": 1,
//       "types": { "PointLight": 1, "Rotator": 2, "Transform": 1 },
//       "entities": [
//         {
//           "id": "1c5e09b7f3a8d264",
//           "name": "Lamp",
//           "children": ["7a21e4c90d3b5f86"],
//           "components": {
//             "PointLight": { "color": [1, 0.5, 0], "intensity": 200 },
//             "Transform": { "location": [0, 0, 1.5], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] }
//           },
//           "scripts": [{ "Rotator": { "speed": 1 } }]
//         },
//         {
//           "id": "3b0f5e2a9c1d4e77",
//           "name": "Lamp Prefab",
//           "children": [],
//           "components": { "SceneInstance": { "scene": "5d2c8e41a7f09b36" }, "Transform": { ... } },
//           "overrides": { "7a21e4c90d3b5f86": { "PointLight": { "intensity": 300 } } }
//         }
//       ]
//     }
//
// - The header: the file type, the scene's asset ID, the version of this layout, and the version of every
//   component and script type the file uses. Older type versions are upgraded through the type's migrations
//   when the file is read; a version newer than the registered type is an error.
// - A flat list of entities, written sorted by ID. `children` is the only hierarchy data and holds the sibling
//   order; the parent is derived. `scripts` (only written when there are any) keeps their attach order.
// - Components and scripts are written by registered name, sorted by name, with every registered field in
//   registration order. Reading accepts earlier names (aliases), any order and missing fields (they keep their
//   defaults); unknown types and fields are errors.
// - `overrides` (only on an entity with a SceneInstance, only written when there are any): field values that
//   replace those of the instance's scene, by the local ID of the entity in that scene, then by component, as
//   `components` are written. Their types are in the header's table, and a partial component object goes
//   through the type's migrations like a whole one. A SceneInstance cannot be overridden (a nested instance
//   keeps its scene).
export namespace aegis::serialize
{
// The version of the scene layout itself (header, entity list), apart from the component types' versions.
inline constexpr std::uint32_t sceneVersion = 1;

// A component or script of a scene entity: its registered type and its fields as an object node, at the
// type's current version. Each field is a member named by the field's current name.
struct SceneComponent
{
    const reflect::TypeInfo* type{ nullptr };
    reflect::Node fields;
};

// The overrides of one entity of an instance's scene: components with only the overridden fields.
struct SceneOverrides
{
    std::uint64_t entity{ 0 };              // Its local ID in the instance's scene
    std::vector<SceneComponent> components; // Sorted by type name
    std::uint32_t line{ 0 };
};

struct SceneEntity
{
    std::uint64_t id{ 0 };
    std::string name;
    std::vector<std::uint64_t> children;    // In sibling order
    std::vector<SceneComponent> components; // Sorted by type name
    std::vector<SceneComponent> scripts;    // In attach order
    std::vector<SceneOverrides> overrides;  // Sorted by entity; only with a SceneInstance
    std::uint32_t line{ 0 };                // Where the entity starts in the file it was read from
};

// A scene asset: the entities of a scene file, checked against the type registry. Never live; instantiate it
// into a world.
struct Scene
{
    static constexpr std::string_view assetType{ "Scene" }; // For the editor (reflect::FieldInfo::assetType)

    std::uint64_t id{ 0 };
    std::vector<SceneEntity> entities; // Sorted by ID
};

// One field of an instance's entity set to a value of its own, replacing the scene's: the scene file's
// `overrides`. Only entities of the instance's own scene can be overridden, not those of the instances it holds.
struct InstanceOverride
{
    std::uint64_t entity{ 0 }; // Its local ID in the instance's scene
    std::string component;     // The registered component type, by its current name
    std::string field;         // By its current name
    reflect::Value value;

    friend auto operator==(const InstanceOverride&, const InstanceOverride&) -> bool = default;
};

// The entity holding an instance of `scene`: instantiating it creates the scene's entities as its descendants
// (the scene's roots become its children), with IDs derived from its own (deriveEntityId), then writes the
// overrides to them. Saving writes the holder with this component and its overrides, not the entities of the
// instance. A scene may hold instances of other scenes.
struct SceneInstance
{
    assets::AssetRef<Scene> scene;
    std::vector<InstanceOverride> overrides; // Sorted by entity, component and field; not a registered field

    // The override of a field, or null.
    [[nodiscard]] auto findOverride(std::uint64_t entity, std::string_view component, std::string_view field) const -> const InstanceOverride*;
    // Adds the override, or replaces the value of the one for the same field.
    void setOverride(InstanceOverride override);
    // Removes the override of a field; false if there was none.
    auto removeOverride(std::uint64_t entity, std::string_view component, std::string_view field) -> bool;
};

// An entity created by a scene instance, so saveScene leaves it out (its holder recreates it): `holder` is the
// world ID of the entity holding the instance, `localId` the entity's ID in the instance's scene (what overrides
// name it by). An entity of a nested instance names the nested holder. Not registered; it is rebuilt at
// instantiation.
struct InstanceMember
{
    std::uint64_t holder{ 0 };
    std::uint64_t localId{ 0 };
};

// Registers SceneInstance with world::registerComponent, so scene files can hold instances.
auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>;

// The world ID of the entity `localId` of a scene instance held by the entity `holderId` (its world ID):
// XXH3_64 of both IDs as little-endian bytes, holder first, with seed 0. Nested instances chain it. Saved games
// and overrides name instance entities by it, so it must never change (a test pins it).
[[nodiscard]] auto deriveEntityId(std::uint64_t holderId, std::uint64_t localId) -> std::uint64_t;

// The value the scene gives the field `field` of the component `type` of its entity `localId`: the file's, or a
// default instance's if the file leaves out the field or the component (every entity has a Transform). What
// reverting an override writes back. Fails with MissingEntity for no such entity.
[[nodiscard]] auto sceneValue(const Scene& scene, std::uint64_t localId, const reflect::TypeInfo& type, const reflect::FieldInfo& field)
    -> std::expected<reflect::Value, Error>;

// Whether `root` or one of its descendants holds an instance of the scene `scene`, directly or through the
// instances it holds: a new instance in a world saved as `scene` must not.
[[nodiscard]] auto holdsScene(world::Registry& registry, std::uint64_t scene, world::Entity root) -> bool;

// Reads a scene file and checks it against the registry: its layout, every type and field, every value's
// kind, entity IDs (unique and non-zero), children (existing, each listed once), the hierarchy (no cycle) and
// overrides (only on an entity with a SceneInstance).
// Errors carry the line they are about.
[[nodiscard]] auto parseScene(std::string_view text, const reflect::TypeRegistry& types) -> std::expected<Scene, Error>;

// The scene file of `scene`: the same input gives the same bytes.
[[nodiscard]] auto writeScene(const Scene& scene) -> std::string;

// Captures every entity of `registry` with its name, children, registered components (those registered with
// world::registerComponent) and scripts, as the scene `id`. The entities of scene instances are left out: the
// holder's SceneInstance recreates them.
[[nodiscard]] auto saveScene(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id) -> std::expected<Scene, Error>;

// Captures `root` and its descendants like the overload above, as the scene `id` with `root` as its one root (its
// own parent is left out). The editor's structural undo and Duplicate keep subtrees this way. `root` must not be
// an entity of a scene instance (capture the holder).
[[nodiscard]] auto saveScene(world::Registry& registry, const reflect::TypeRegistry& types, world::Entity root, std::uint64_t id)
    -> std::expected<Scene, Error>;

// Finishes a component read from a scene file before it joins its entity, and a script once its fields are read:
// `component` is an instance of `type` with the file's fields set. The asset library loads the asset references
// here, which the file gives as IDs, including the scene of a SceneInstance.
using PrepareComponent = std::function<void(const reflect::TypeInfo& type, void* component)>;

// Creates the scene's entities in `registry`, keeping their IDs (a world's root scene, a level), with their
// components, scripts and hierarchy, then the entities of the scene instances they hold. Each component is
// built complete (its fields read, then `prepare`) before it is added, so observers of its construction see the
// final value; a script gets `prepare` after its fields are read, so its asset references are loaded before its
// onCreate runs. Without `prepare`, asset references stay unresolved (an ID without a slot). An instance's
// overrides are written to its components before `prepare`; one whose entity or component is gone from the
// instance's scene, or whose value the field's setter refuses, is skipped with a warning and kept on the holder
// (parseScene already checked its type, field and kind). A singleton
// component (the sky, the main camera) the world already has is dropped with a warning. On failure (an ID the
// registry already uses, a value that does not fit its field, a type that cannot be created, an instance whose
// scene did not load, a scene that holds itself through its instances) nothing is created.
[[nodiscard]] auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const Scene& scene,
    const PrepareComponent& prepare = {}) -> std::expected<void, Error>;

// The IDs a subtree's entities get when it is instantiated.
enum class EntityIds
{
    Keep, // The saved ones: undo restores a destroyed subtree
    New,  // New random ones, with the children lists remapped: a duplicate or a pasted copy
};

// Creates `subtree`, a scene with one root (from saveScene of an entity), with its components, scripts, hierarchy
// and the instances its entities hold, and makes its root the child of `parent` at `index` among its children
// (the last one for an index past their end; a root entity for a null `parent`). Otherwise like the overloads
// around it: on failure (with EntityIds::Keep, an ID the registry already uses) nothing is created. Returns the
// root.
[[nodiscard]] auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const Scene& subtree, world::Entity parent,
    std::size_t index, EntityIds ids, const PrepareComponent& prepare = {}) -> std::expected<world::Entity, Error>;

// Creates an instance of `scene` (a loaded reference): a new entity holds it, with the ID `holderId` (a random one
// for 0), as a child of `parent` (a root entity for a null `parent`), and the scene's entities derive their IDs from
// the holder's. Otherwise like the overload above (a `holderId` the registry already uses: DuplicateEntity). Returns
// the holder.
[[nodiscard]] auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const assets::AssetRef<Scene>& scene,
    world::Entity parent, const PrepareComponent& prepare = {}, std::uint64_t holderId = 0) -> std::expected<world::Entity, Error>;
}

// Module-internal: shared with :model.
namespace aegis::serialize::detail
{
// The registered fields of `object`, an instance of `type`, as a scene component.
[[nodiscard]] auto saveFields(const reflect::TypeInfo& type, const void* object) -> std::expected<SceneComponent, Error>;
}
