module;
#include <memory>

export module aegis.assets:components;
import :material;
import :static_mesh;
import :texture;

export namespace aegis::assets
{
// Scene components referencing assets. The old renderer draws Aegis::Graphics' equivalents, so
// entities carrying these are invisible to it until the Phase 4 cutover.

struct Mesh
{
    std::shared_ptr<StaticMesh> staticMesh;
};

struct Material
{
    std::shared_ptr<MaterialInstance> instance;
};

struct Environment
{
    std::shared_ptr<Texture> skybox;
    std::shared_ptr<Texture> irradiance;
    std::shared_ptr<Texture> prefiltered;
    std::shared_ptr<Texture> brdfLUT;
};
}
