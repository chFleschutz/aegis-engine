module;
#include <expected>
#include <filesystem>
#include <format>

module aegis.assets;
import aegis.scene.registry;

namespace aegis::assets
{
auto Loader::load(const Desc& desc, const std::filesystem::path& path) -> std::expected<Aegis::Scene::Entity, Error>
{
    if (!std::filesystem::exists(path))
        return makeError(ErrorCode::FileNotFound, std::format("Model file does not exist: '{}'", path.string()));

    const auto extension = path.extension();
    if (extension == ".gltf" || extension == ".glb")
        return loadGltf(desc, path);
    if (extension == ".obj")
        return loadObj(desc, path);

    return makeError(ErrorCode::UnsupportedFormat, std::format("Unsupported model format: '{}'", path.string()));
}
}
