export module aegis.defaults;

import std;
import aegis.world;
import aegis.world.render_components;
import aegis.scene_description;
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
auto addDefaultCamera(SceneContext& context) -> world::Entity;

/// @brief An ambient light and a directional light
auto addDefaultLights(SceneContext& context) -> DefaultLights;

/// @brief The engine's default environment as the sky
auto addDefaultEnvironment(SceneContext& context) -> world::Entity;

/// @brief The camera, the lights and the environment above
void addDefaults(SceneContext& context);
}
