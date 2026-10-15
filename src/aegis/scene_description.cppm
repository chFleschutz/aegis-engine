module;

#include <concepts>

export module aegis.scene_description;

import aegis.scene;
import aegis.scripting.script_manager;

export namespace Aegis
{
/// @brief Base class for creating a scene
class SceneDescription
{
public:
    virtual ~SceneDescription() = default;

    /// @brief All objects in a scene are created here
    virtual void initialize(Scene::Scene& scene, Scripting::ScriptManager& scripts) = 0;
};

template<typename T>
concept SceneDescriptionDerived = std::derived_from<T, SceneDescription>;
}
