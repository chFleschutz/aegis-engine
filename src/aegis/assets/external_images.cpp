module aegis.assets;
import std;
import aegis.core;

namespace aegis::assets
{
namespace
{
auto toPath(std::string_view text) -> std::filesystem::path
{
    return std::filesystem::path{ std::u8string_view{ reinterpret_cast<const char8_t*>(text.data()), text.size() } };
}

auto toText(const std::filesystem::path& path) -> std::string
{
    const auto text = path.generic_u8string();
    return { reinterpret_cast<const char*>(text.data()), text.size() };
}

auto describeUsage(bool srgb) -> std::string_view
{
    return srgb ? "color (sRGB)" : "linear data";
}
}

auto resolveExternalImages(const AssetDatabase& database, const AssetEntry& modelEntry, ModelData& model) -> void
{
    for (auto& image : model.images)
    {
        if (image.uri.empty())
            continue;

        const auto path = detail::externalImagePath(modelEntry.path, image.uri);
        const auto id = path ? database.find(modelEntry.root, *path) : std::nullopt;
        const auto* entry = id ? database.find(*id) : nullptr;
        if (!entry || entry->type != "Texture")
        {
            log::warn("'{}' uses the image '{}', which is no texture asset; its materials go without it", modelEntry.path,
                path.value_or(image.uri));
            continue;
        }

        image.texture = entry->id;
        if (const auto srgb = entry->settings.srgb.value_or(true); srgb != image.srgb)
        {
            log::warn("'{}' uses '{}' as {}, but the image is imported as {}; set \"srgb\": {} in its sidecar to match",
                modelEntry.path, entry->path, describeUsage(image.srgb), describeUsage(srgb), image.srgb);
        }
    }
}

auto detail::externalImagePath(std::string_view modelPath, std::string_view uri) -> std::optional<std::string>
{
    const auto relative = toPath(uri);
    if (relative.has_root_path())
        return std::nullopt;
    const auto path = (toPath(modelPath).parent_path() / relative).lexically_normal();
    if (path.empty() || *path.begin() == "..")
        return std::nullopt;
    return toText(path);
}
}
