export module aegis.serialize:scene;
import std;
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

// Reads a scene file and checks it against the registry: its layout, every type and field, every value's
// kind, entity IDs (unique and non-zero), children (existing, each listed once) and the hierarchy (no cycle).
// Errors carry the line they are about.
[[nodiscard]] auto parseScene(std::string_view text, const reflect::TypeRegistry& types) -> std::expected<Scene, Error>;

// The scene file of `scene`: the same input gives the same bytes.
[[nodiscard]] auto writeScene(const Scene& scene) -> std::string;

// Captures every entity of `registry` with its name, children, registered components (those registered with
// world::registerComponent) and scripts, as the scene `id`.
[[nodiscard]] auto saveScene(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id) -> std::expected<Scene, Error>;

// Finishes a component read from a scene file before it joins its entity: `component` is an instance of `type`
// with the file's fields set. The asset library loads the asset references here, which the file gives as IDs.
using PrepareComponent = std::function<void(const reflect::TypeInfo& type, void* component)>;

// Creates the scene's entities in `registry`, keeping their IDs (a world's root scene), with their components,
// scripts and hierarchy. Each component is built complete (its fields read, then `prepare`) before it is added,
// so observers of its construction see the final value; without `prepare`, asset references stay unresolved
// (an ID without a slot). On failure (an ID the registry already uses, a value that does not fit its field, a
// type that cannot be created) nothing is created.
[[nodiscard]] auto instantiate(world::Registry& registry, const reflect::TypeRegistry& types, const Scene& scene,
    const PrepareComponent& prepare = {}) -> std::expected<void, Error>;
}
