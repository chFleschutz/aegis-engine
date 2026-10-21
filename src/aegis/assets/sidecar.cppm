export module aegis.assets:sidecar;
import std;
import :asset_id;
import :error;

export namespace aegis::assets
{
/// @brief Import settings of a model sidecar (the fields the importers read; see docs/asset-system.md).
struct ImportSettings
{
    std::string upAxis{ "Y" };
    std::string handedness{ "Right" };
    double scale{ 1.0 };
    bool strictNames{ false };
    /// @brief Textures: whether the pixels are color data (sRGB) or linear; none means the importer's default.
    std::optional<bool> srgb;

    [[nodiscard]] friend auto operator==(const ImportSettings&, const ImportSettings&) -> bool = default;
};

/// @brief The `<file>.<ext>.meta` next to a foreign source file: the file's main ID, the importer and its
///        settings, and the `key → id` table of its parts (see SubAssetKeys). Entries of parts that were
///        removed stay, so a part that comes back keeps its ID.
struct Sidecar
{
    AssetId id; // The file's main asset (a glTF's scene, a PNG's texture)
    std::string importer;
    std::uint32_t importerVersion{ 1 };
    ImportSettings settings;
    std::map<std::string, AssetId> subAssets; // Sorted, so saving an unchanged sidecar gives the same bytes

    /// @brief The ID of the part 'key', made and recorded if the table has none.
    auto subAssetId(const std::string& key) -> AssetId;

    [[nodiscard]] friend auto operator==(const Sidecar&, const Sidecar&) -> bool = default;
};

/// @brief `<source>.meta`: the full file name plus ".meta", so Helmet.gltf and Helmet.png do not collide.
[[nodiscard]] auto sidecarPath(const std::filesystem::path& source) -> std::filesystem::path;

/// @brief Parses a sidecar (JSON; comments allowed). IDs must be 16 lowercase hex digits and the file's own
///        ID must be set.
/// @return ErrorCode::DecodeFailed for malformed JSON, ErrorCode::InvalidData for a bad or missing ID.
[[nodiscard]] auto parseSidecar(std::string_view json) -> std::expected<Sidecar, Error>;

/// @brief The sidecar as pretty-printed JSON with a fixed key order and sorted sub-asset table.
[[nodiscard]] auto writeSidecar(const Sidecar& sidecar) -> std::string;

/// @brief The `{type, id, version}` header every native file starts with. Other keys are ignored.
struct NativeHeader
{
    std::string type;
    AssetId id;
    std::uint32_t version{ 0 };
};

/// @return ErrorCode::DecodeFailed / InvalidData like parseSidecar.
[[nodiscard]] auto parseNativeHeader(std::string_view json) -> std::expected<NativeHeader, Error>;
}
