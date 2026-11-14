module aegis.assets;
import std;

namespace aegis::assets
{
auto embeddedImageKeys(const ModelData& model, SubAssetKeys& keys) -> std::expected<std::vector<std::string>, Error>
{
    std::vector<std::string> result(model.images.size());
    std::size_t position = 0;
    for (std::size_t i = 0; i < model.images.size(); ++i)
    {
        if (!model.images[i].uri.empty())
            continue;
        auto key = keys.name("Texture", model.images[i].name, position++);
        if (!key)
            return std::unexpected{ std::move(key.error()) };
        result[i] = std::move(*key);
    }
    return result;
}

auto applyEmbeddedImageSettings(const Sidecar& sidecar, std::span<const std::string> imageKeys, ModelData& model) -> void
{
    for (std::size_t i = 0; i < model.images.size() && i < imageKeys.size(); ++i)
    {
        if (imageKeys[i].empty())
            continue;
        if (const auto settings = sidecar.partSettings.find(imageKeys[i]); settings != sidecar.partSettings.end())
            model.images[i].settings = settings->second;
    }
}
}
