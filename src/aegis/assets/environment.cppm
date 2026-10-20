export module aegis.assets:environment;
import std;
import :texture;

export namespace aegis::assets
{
/// @brief The image-based lighting of a sky: its cube map and the maps derived from it. The BRDF LUT
///        does not depend on the environment and is owned by the renderer.
struct Environment
{
    std::shared_ptr<Texture> skybox;
    std::shared_ptr<Texture> irradiance;
    std::shared_ptr<Texture> prefiltered;
};
}
