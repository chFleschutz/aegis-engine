export module aegis.assets:external_images;
import std;
import :asset_database;
import :model_data;

// External images of a model (docs/asset-system.md, "Identity"): an image file a glTF names by URI is an asset of its
// own, with its own ID and import settings, which every model that uses it and every material file shares. Only
// images embedded in a model are parts of it.

export namespace aegis::assets
{
/// @brief Gives each external image of 'model', imported from the file of 'modelEntry', the ID of the Texture asset
///        its URI names (ModelImage::texture), looked up in 'database' relative to the model file's folder.
///        An image no Texture asset has is logged with aegis::log::warn and keeps no ID; its materials go without
///        it. An image whose import settings disagree with the usage the model's materials imply (a normal map
///        imported as sRGB) is logged once, naming both files; the image's own settings are used.
auto resolveExternalImages(const AssetDatabase& database, const AssetEntry& modelEntry, ModelData& model) -> void;
}

export namespace aegis::assets::detail
{
/// @brief The path, relative to the root and with '/', of the file 'uri' names relative to the model file at
///        'modelPath' (relative to the same root). None if it leaves the root or is absolute.
[[nodiscard]] auto externalImagePath(std::string_view modelPath, std::string_view uri) -> std::optional<std::string>;
}
