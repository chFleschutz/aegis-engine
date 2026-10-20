export module aegis.world.render_components;

import std;
import aegis.assets;

// The components that bind an entity to the assets it is drawn with. They live with the scene (not in
// aegis.assets, which knows no scene) so the render graph can read them and the app layer can create them.

export namespace aegis
{
struct MeshRenderer
{
    std::shared_ptr<assets::StaticMesh> mesh;
    std::shared_ptr<assets::Material> material;
};
}
