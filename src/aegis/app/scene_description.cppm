module;

// Keeps std::derived_from visible to this interface (see AGENTS.md, import std)
#include <concepts>

export module aegis.scene_description;

import std;
import aegis.asset_library;
import aegis.assets;
import aegis.world;
import aegis.reflect;

export namespace aegis
{
/// @brief What a scene description gets to build its scene with. Plain references, owned by the engine.
///        Scripts are attached through `types` (scripting::attachScript), by registered name.
struct SceneContext
{
    world::World& scene;
    const reflect::TypeRegistry& types;
    AssetLibrary& assets;
};

/// @brief Instantiates the scene or model file 'scene' under 'parent' (see AssetLibrary::instantiateScene), for
///        scene descriptions. A model that fails to load is no failure; it is replaced by the default cube.
/// @return The entity holding the instance (move it to place the model), or why the scene could not be
///         instantiated; the scene is then unchanged.
[[nodiscard]] auto instantiate(SceneContext& context, assets::AssetId scene, world::Entity parent = {})
    -> std::expected<world::Entity, AssetLibraryError>;

/// @brief Base class for creating a scene
class SceneDescription
{
public:
    virtual ~SceneDescription() = default;

    /// @brief All objects in a scene are created here
    virtual void initialize(SceneContext& context) = 0;
};

template<typename T>
concept SceneDescriptionDerived = std::derived_from<T, SceneDescription>;
}
