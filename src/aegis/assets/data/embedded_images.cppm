export module aegis.assets:embedded_images;
import std;
import :error;
import :model_data;
import :sidecar;
import :sub_asset_key;

// Embedded images of a model (docs/asset-system.md, "Settings per part"): an image a model file embeds (GLB chunk,
// data URI) is a part of it, with an entry in the model's sidecar that can carry the settings a texture file has.

export namespace aegis::assets
{
/// @brief The sub-asset keys (`Texture/<name>`) of the images of 'model', by ModelData::images index; empty for an
///        external image, which is no part of the model. Embedded images are numbered among themselves, as
///        Model::textures is.
/// @param keys The keys of the model's parts so far, which get these keys added.
/// @return The error of SubAssetKeys::name for a bad name under strictNames.
[[nodiscard]] auto embeddedImageKeys(const ModelData& model, SubAssetKeys& keys) -> std::expected<std::vector<std::string>, Error>;

/// @brief Gives each embedded image of 'model' the settings its entry in 'sidecar' has (ModelImage::settings), by
///        its key in 'imageKeys' (from embeddedImageKeys). An image whose entry has none keeps the defaults, so it
///        is read as its materials imply.
auto applyEmbeddedImageSettings(const Sidecar& sidecar, std::span<const std::string> imageKeys, ModelData& model) -> void;
}
