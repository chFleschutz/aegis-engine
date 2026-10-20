module;

#include "core/assert.h"

export module aegis.defaults;

import std;
import aegis.world;
import aegis.world.render_components;
import aegis.scene_description;
import aegis.scripting.script;
import aegis.assets;

// Opt-in helpers: a scene calls the ones it wants. The engine adds none of this by itself, so a scene
// without a camera, lights or a sky is possible.
export namespace aegis
{
struct DefaultLights
{
    world::Entity ambient;
    world::Entity directional;
};

/// @brief A main camera with fly controls (the "KinematicMovementController" script)
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
    AGX_ASSERT_X(controller, "The KinematicMovementController script is not registered");
    return camera;
}

/// @brief An ambient light and a directional light
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

/// @brief The engine's default environment as the sky
auto addDefaultEnvironment(SceneContext& context) -> world::Entity
{
    auto& registry = context.scene.registry();
    auto skybox = registry.create("Skybox");
    registry.add<aegis::Sky>(skybox, context.assets.defaultEnvironment());
    return skybox;
}

/// @brief The camera, the lights and the environment above
void addDefaults(SceneContext& context)
{
    addDefaultCamera(context);
    addDefaultLights(context);
    addDefaultEnvironment(context);
}
}
