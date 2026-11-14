module aegis.assets;
import std;

namespace aegis::assets
{
auto importModel(const std::filesystem::path& path) -> std::expected<ModelData, Error>
{
    if (!std::filesystem::exists(path))
        return makeError(ErrorCode::FileNotFound, std::format("Model file does not exist: '{}'", path.string()));

    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });

    const auto addPath = [&](Error error) {
        error.message = std::format("{} ('{}')", error.message, path.string());
        return std::unexpected{ std::move(error) };
    };

    if (extension == ".gltf" || extension == ".glb")
    {
        std::ifstream file{ path, std::ios::binary };
        std::vector<std::byte> bytes(std::filesystem::file_size(path));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return makeError(ErrorCode::DecodeFailed, std::format("Failed to read glTF '{}'", path.string()));
        auto model = importGltf(bytes, path.parent_path(), path.stem().string());
        if (!model)
            return addPath(model.error());
        return model;
    }
    if (extension == ".obj")
    {
        std::ifstream file{ path, std::ios::binary };
        if (!file)
            return makeError(ErrorCode::FileNotFound, std::format("Failed to open OBJ '{}'", path.string()));
        auto model = importObj(file, path.stem().string());
        if (!model)
            return addPath(model.error());
        return model;
    }

    return makeError(ErrorCode::UnsupportedFormat, std::format("Unsupported model format: '{}'", path.string()));
}
}
