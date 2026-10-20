export module aegis.instantiate;

import std;
import aegis.assets;
import aegis.world.components;
import aegis.world.registry;
import aegis.world.render_components;

export namespace aegis
{
/// @brief Creates the entities of a loaded model: a root with the model's rotation, an entity per node below
///        it (a mesh with several primitives gets one child per primitive), each with the Mesh and Material
///        components. The entities share the model's meshes and materials, so calling it N times draws N
///        copies of one load.
/// @return The root entity.
auto instantiate(world::Registry& registry, const assets::Model& model) -> world::Entity;
}
