module aegis.assets;
import std;
import aegis.scene.registry;

namespace aegis::assets
{
auto Loader::load(const Desc& desc, const std::filesystem::path& path) -> std::expected<aegis::scene::Entity, Error>
{
    if (!std::filesystem::exists(path))
        return makeError(ErrorCode::FileNotFound, std::format("Model file does not exist: '{}'", path.string()));

    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    if (extension == ".gltf" || extension == ".glb")
        return loadGltf(desc, path);
    if (extension == ".obj")
        return loadObj(desc, path);

    return makeError(ErrorCode::UnsupportedFormat, std::format("Unsupported model format: '{}'", path.string()));
}
}
