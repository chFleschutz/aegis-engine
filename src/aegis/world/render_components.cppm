export module aegis.world.render_components;

import std;
import aegis.assets;

// The components that bind an entity to the assets it is drawn with. They live with the scene (not in
// aegis.assets, which knows no scene) so the render graph can read them and the app layer can create them.

export namespace aegis
{
struct MeshRenderer
{
    assets::AssetRef<assets::StaticMesh> mesh;
    assets::AssetRef<assets::Material> material;
};

/// @brief The sky of a world: drawn behind the geometry and the source of its image-based lighting.
///        The lighting pass uses the first entity that has one.
struct Sky
{
    assets::AssetRef<assets::Environment> environment;
};
}
