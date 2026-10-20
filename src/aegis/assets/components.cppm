export module aegis.assets:components;
import std;
import :texture;

export namespace aegis::assets
{
// Scene components referencing assets.

struct Environment
{
    std::shared_ptr<Texture> skybox;
    std::shared_ptr<Texture> irradiance;
    std::shared_ptr<Texture> prefiltered;
    std::shared_ptr<Texture> brdfLUT;
};
}
