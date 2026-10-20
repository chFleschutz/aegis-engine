module;

// Keeps std::derived_from visible to this interface (see AGENTS.md, import std)
#include <concepts>

export module aegis.scene_description;

import std;
import aegis.asset_loader;
import aegis.scene;
import aegis.scripting.script_manager;

export namespace aegis
{
/// @brief What a scene description gets to build its scene with. Plain references, owned by the engine.
struct SceneContext
{
    scene::Scene& scene;
    scripting::ScriptManager& scripts;
    AssetLoader& assets;
};

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
