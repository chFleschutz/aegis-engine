export module aegis.serialize:scene;
import std;
import aegis.assets;
import aegis.reflect;
import aegis.world.registry;
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

struct SceneEntity
{
    std::uint64_t id{ 0 };
    std::string name;
    std::vector<std::uint64_t> children;    // In sibling order
    std::vector<SceneComponent> components; // Sorted by type name
    std::vector<SceneComponent> scripts;    // In attach order
    std::uint32_t line{ 0 };                // Where the entity starts in the file it was read from
};

// A scene asset: the entities of a scene file, checked against the type registry. Never live; instantiate it
// into a world.
struct Scene
{
    std::uint64_t id{ 0 };
    std::vector<SceneEntity> entities; // Sorted by ID
};

// The entity holding an instance of `scene`: instantiating it creates the scene's entities as its descendants
// (the scene's roots become its children), with IDs derived from its own (deriveEntityId). Saving writes the
// holder with this component, not the entities of the instance. A scene may hold instances of other scenes.
struct SceneInstance
{
    assets::AssetRef<Scene> scene;
};

// An entity created by a scene instance, so saveScene leaves it out (its holder recreates it): `holder` is the
// world ID of the entity holding the instance. Not registered; it is rebuilt at instantiation.
struct InstanceMember
{
    std::uint64_t holder{ 0 };
};

// Registers SceneInstance with world::registerComponent, so scene files can hold instances.
auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>;

// The world ID of the entity `localId` of a scene instance held by the entity `holderId` (its world ID):
// XXH3_64 of both IDs as little-endian bytes, holder first, with seed 0. Nested instances chain it. Saved games
// and overrides name instance entities by it, so it must never change (a test pins it).
[[nodiscard]] auto deriveEntityId(std::uint64_t holderId, std::uint64_t localId) -> std::uint64_t;

// Reads a scene file and checks it against the registry: its layout, every type and field, every value's
// kind, entity IDs (unique and non-zero), children (existing, each listed once) and the hierarchy (no cycle).
// Errors carry the line they are about.
[[nodiscard]] auto parseScene(std::string_view text, const reflect::TypeRegistry& types) -> std::expected<Scene, Error>;

// The scene file of `scene`: the same input gives the same bytes.
[[nodiscard]] auto writeScene(const Scene& scene) -> std::string;

// Captures every entity of `registry` with its name, children, registered components (those registered with
// world::registerComponent) and scripts, as the scene `id`. The entities of scene instances are left out: the
// holder's SceneInstance recreates them.
[[nodiscard]] auto saveScene(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id) -> std::expected<Scene, Error>;

// Finishes a component read from a scene file before it joins its entity: `component` is an instance of `type`
// with the file's fields set. The asset library loads the asset references here, which the file gives as IDs,
// including the scene of a SceneInstance.
using PrepareComponent = std::function<void(const reflect::TypeInfo& type, void* component)>;

// Creates the scene's entities in `registry`, keeping their IDs (a world's root scene, a level), with their
// components, scripts and hierarchy, then the entities of the scene instances they hold. Each component is
// built complete (its fields read, then `prepare`) before it is added, so observers of its construction see the
// final value; without `prepare`, asset references stay unresolved (an ID without a slot). A singleton
// component (the sky, the main camera) the world already has is dropped with a warning. On failure (an ID the
// registry already uses, a value that does not fit its field, a type that cannot be created, an instance whose
// scene did not load, a scene that holds itself through its instances) nothing is created.
[[nodiscard]] auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const Scene& scene,
    const PrepareComponent& prepare = {}) -> std::expected<void, Error>;

// Creates an instance of `scene` (a loaded reference): a new entity with a random ID holds it, as a child of
// `parent` (a root entity for a null `parent`), and the scene's entities derive their IDs from the holder's.
// Otherwise like the overload above. Returns the holder.
[[nodiscard]] auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const assets::AssetRef<Scene>& scene,
    world::Entity parent, const PrepareComponent& prepare = {}) -> std::expected<world::Entity, Error>;
}

// Module-internal: shared with :model.
namespace aegis::serialize::detail
{
// The registered fields of `object`, an instance of `type`, as a scene component.
[[nodiscard]] auto saveFields(const reflect::TypeInfo& type, const void* object) -> std::expected<SceneComponent, Error>;
}
