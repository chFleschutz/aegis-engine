export module aegis.serialize:model;
import std;
import aegis.assets;
import aegis.reflect;
import :error;
import :scene;

// An imported model as a scene asset (docs/asset-system.md, "Scenes are prefabs"): a glTF or OBJ file's main
// asset is the scene of its node hierarchy, instantiated like any scene.
export namespace aegis::serialize
{
// The local ID of a model's entity by its key (see modelScene). The asset library reads and records them in
// the model's sidecar, next to the IDs of its parts.
using ModelEntityId = std::function<std::uint64_t(const std::string& key)>;

// The scene `id` of a loaded model: a root entity named after the model, with its root rotation (the file's up
// axis to the engine's) and the root mesh, and an entity per node below it, with the node's name, transform and
// mesh. A mesh of one primitive goes on its entity; several become child entities, one per primitive, named
// `<name>_Submesh_<index>`. Every entity has a Transform, and a mesh entity a MeshRenderer naming the
// primitive's mesh and material by ID.
//
// Entities are keyed like sub-assets (assets::SubAssetKeys): `Entity/<node name>` (the root:
// SubAssetKeys::rootEntity), plus `/<index>` for a primitive's entity. `entityId` gives each key's local ID, so
// IDs read from the sidecar survive reimports. An unnamed or repeated node name warns, or with `strictNames`
// fails with InvalidData. Fails with UnknownType if `types` lacks Transform or MeshRenderer
// (world::registerComponents), and with DuplicateEntity if `entityId` gives an ID twice.
[[nodiscard]] auto modelScene(const assets::Model& model, std::uint64_t id, const reflect::TypeRegistry& types, const ModelEntityId& entityId,
    bool strictNames = false) -> std::expected<Scene, Error>;
}
