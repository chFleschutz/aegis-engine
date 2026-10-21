#include <aegis/launch/game_module.h>

import std;
import aegis.engine;

namespace
{
/// @brief An empty scene with the default camera, lights and sky
class TemplateScene : public aegis::SceneDescription
{
public:
    /// @brief All objects in a scene are created here
    void initialize(aegis::SceneContext& context) override
    {
        [[maybe_unused]] auto& [scene, types, assets] = context;
        aegis::addDefaults(context);
        using namespace aegis;

        // Create your scene here
    }
};

/// @brief Register your components and scripts here
auto registerTypes([[maybe_unused]] aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>
{
    return {};
}

auto onStartup(aegis::Engine& engine) -> std::expected<void, aegis::EngineError>
{
    engine.loadScene<TemplateScene>();
    return {};
}
}

AEGIS_GAME_MODULE({ .name = "Template", .registerTypes = registerTypes, .onStartup = onStartup })
