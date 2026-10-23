module;

#include <cassert>

module aegis.defaults;

import std;
import aegis.scripting.script;

namespace aegis
{
auto addDefaultCamera(SceneContext& context) -> world::Entity
{
    auto& registry = context.scene.registry();
    auto camera = registry.create("Main Camera");
    registry.add<Camera>(camera);
    registry.add<DynamicTag>(camera);
    registry.get<Transform>(camera) = Transform{
        .location = { 0.0f, -15.0f, 10.0f },
        .rotation = glm::radians(glm::vec3{ -30.0f, 0.0f, 0.0f })
    };
    context.scene.setMainCamera(camera);

    [[maybe_unused]] auto controller = scripting::attachScript(registry, context.types, camera, "KinematicMovementController");
    assert(controller && "The KinematicMovementController script is not registered");
    return camera;
}

auto addDefaultLights(SceneContext& context) -> DefaultLights
{
    auto& registry = context.scene.registry();

    auto ambient = registry.create("Ambient Light");
    registry.add<AmbientLight>(ambient);

    auto directional = registry.create("Directional Light");
    registry.add<DirectionalLight>(directional);
    registry.get<Transform>(directional).rotation = glm::radians(glm::vec3{ 60.0f, 0.0f, 45.0f });

    return { ambient, directional };
}

auto addDefaultEnvironment(SceneContext& context) -> world::Entity
{
    auto& registry = context.scene.registry();
    auto skybox = registry.create("Skybox");
    registry.add<aegis::Sky>(skybox, context.assets.defaultEnvironment());
    return skybox;
}

void addDefaults(SceneContext& context)
{
    addDefaultCamera(context);
    addDefaultLights(context);
    addDefaultEnvironment(context);
}
}
