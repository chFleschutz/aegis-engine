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

/// @brief Whether a texture with 'settings' is read as sRGB: its srgb setting, else 'usage', the default its kind
///        implies (color for a texture file, the usage its materials imply for an image embedded in a model).
///        The one rule for external and embedded images.
[[nodiscard]] auto textureSrgb(const ImportSettings& settings, bool usage) -> bool;

/// @brief The `<file>.<ext>.meta` next to a foreign source file: the file's main ID, the importer and its
///        settings, and the `key → id` table of its parts (see SubAssetKeys). Entries of parts that were
///        removed stay, so a part that comes back keeps its ID. A part can have settings of its own, the ones a
///        standalone asset of its type has (an embedded image's color space).
struct Sidecar
{
    AssetId id; // The file's main asset (a glTF's scene, a PNG's texture)
    std::string importer;
    std::uint32_t importerVersion{ 1 };
    ImportSettings settings;
    std::map<std::string, AssetId> subAssets; // Sorted, so saving an unchanged sidecar gives the same bytes
    /// @brief The settings of the parts in subAssets that have any, by key. In the file a part's entry is its ID, or
    ///        `{ "id", "settings" }` for a part listed here; only the settings that differ from the defaults are written.
    std::map<std::string, ImportSettings> partSettings;

    /// @brief The ID of the part 'key': the table's entry, else derived (deriveSubAssetId) from 'id' and
    ///        recorded. A recorded ID is never recomputed, so older random IDs stay as they are.
    auto subAssetId(const std::string& key) -> AssetId;

    [[nodiscard]] friend auto operator==(const Sidecar&, const Sidecar&) -> bool = default;
};

/// @brief The ID of a new part 'key' of the file 'file': XXH3_64 of the file's ID as little-endian bytes followed
///        by the key's bytes (escaped, UTF-8), with seed 0. Every machine and branch that meets the same new part
///        derives the same ID, so their sidecars agree. Recorded IDs are data, so it must never change (a test pins
///        it). A result of zero, or one that equals another part's ID, is as unlikely as a random collision and
///        rejected the same way, when the sidecar is read or scanned.
[[nodiscard]] auto deriveSubAssetId(AssetId file, std::string_view key) -> AssetId;

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
