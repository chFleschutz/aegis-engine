export module aegis.scene_description;

import std;
import aegis.scene;
import aegis.scripting.script_manager;

export namespace aegis
{
/// @brief Base class for creating a scene
class SceneDescription
{
public:
    virtual ~SceneDescription() = default;

    /// @brief All objects in a scene are created here
    virtual void initialize(scene::Scene& scene, scripting::ScriptManager& scripts) = 0;
};

template<typename T>
concept SceneDescriptionDerived = std::derived_from<T, SceneDescription>;
}
