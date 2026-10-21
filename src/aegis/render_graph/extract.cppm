export module aegis.render_graph:extract;
import std;
import :frame_scene;
import aegis.world;

export namespace aegis::render_graph
{
/// @brief The FrameScene of a world whose transforms and camera matrices are current (after its update):
///        the main camera if it has a Camera and a GlobalTransform, the ambient and directional light,
///        every point light and the environment of the first aegis::Sky. Reads the world, never writes it.
[[nodiscard]] auto extract(aegis::world::World& world) -> FrameScene;
}
