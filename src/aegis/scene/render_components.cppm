export module aegis.scene.render_components;

import std;
import aegis.assets;

// The components that bind an entity to the assets it is drawn with. They live with the scene (not in
// aegis.assets, which knows no scene) so the render graph can read them and the app layer can create them.

export namespace aegis
{
struct Mesh
{
    std::shared_ptr<assets::StaticMesh> staticMesh;
};

struct Material
{
    std::shared_ptr<assets::MaterialInstance> instance;
};
}
