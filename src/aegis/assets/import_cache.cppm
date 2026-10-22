export module aegis.assets:import_cache;
import std;
import :asset_id;
import :error;
import :model_data;
import :sidecar;

// The import cache (docs/asset-system.md, "Import cache and cooking"): what importing a source file produced, kept
// in the project's `.aegis/imported/` so a later load reads it instead of importing again. Never committed and
// safe to delete. One folder per source file, named by the file's asset ID:
//
//   imported/<id>/model.json       the model entry: its key, the files it depends on, the node tree, materials,
//                                  images and the blob of each primitive
//   imported/<id>/mesh-<m>-<p>.mesh the mesh blob (see :mesh_blob) of primitive p of mesh m
//   imported/<id>/image-<i>.bin    the encoded bytes of embedded image i; an external image is an asset of its
//                                  own and only its URI is kept
//
// An entry is used only if its key, an XXH3-64 hash of the source and its dependencies, the import settings, the
// importer version and the platform, matches the sources as they are now. A reimport writes a new folder next to
// the old one and moves it into place, so other processes never read a partly written entry.

export namespace aegis::assets
{
/// @brief The version of the model importer, part of every model's cache key: bump it when importing the same file
///        gives other data (a new vertex attribute, other meshlet limits), so every entry is made again.
inline constexpr std::uint32_t modelImporterVersion{ 2 };

/// @brief The platform the cache's data is made for, part of every key.
#ifdef _WIN32
inline constexpr std::string_view importPlatform{ "windows" };
#else
inline constexpr std::string_view importPlatform{ "linux" };
#endif

/// @brief What a cache key is made from.
struct ImportCacheKeyDesc
{
    std::string_view importer; // "model"
    std::uint32_t importerVersion{ 0 };
    ImportSettings settings;
    std::string_view platform{ importPlatform };
    /// @brief The source file's bytes, then each dependency's in order (ModelData::dependencies); none for a file
    ///        that does not exist.
    std::vector<std::optional<std::span<const std::byte>>> files;
};

/// @brief The cache key: an XXH3-64 hash of every field of 'desc', so changing any of them gives another key.
[[nodiscard]] auto importCacheKey(const ImportCacheKeyDesc& desc) -> std::uint64_t;

class ImportCache
{
public:
    /// @param directory The cache's folder (`<project>/.aegis/imported`); empty for none, which always imports.
    explicit ImportCache(std::filesystem::path directory = {}) :
        m_directory{ std::move(directory) }
    {
    }

    struct ModelLoad
    {
        ModelData model;
        bool cached{ false }; // Read from the cache; false if the file was imported
    };

    /// @brief The model file 'source' (asset 'id'), imported with 'settings': read from its cache entry if the key
    ///        still matches, else imported (importModel) and written to the entry. An entry that cannot be read
    ///        is imported again; one that cannot be written is logged and the import is returned.
    /// @return The errors of importModel.
    [[nodiscard]] auto loadModel(AssetId id, const std::filesystem::path& source, const ImportSettings& settings) const
        -> std::expected<ModelLoad, Error>;

    /// @brief The cache entry of model file 'id' as text: the entry's header, key and dependencies, counts and a
    ///        line per primitive. With 'meshKey' (a StaticMesh sub-asset key, `StaticMesh/<mesh>/<primitive>`) only
    ///        that primitive's mesh blob, in detail: its header, bounds, the first vertices and meshlets.
    /// @return ErrorCode::FileNotFound if there is no entry (the model has not been loaded since the cache was
    ///         cleared), ErrorCode::InvalidArgument for a key no primitive has, or the entry's read errors.
    [[nodiscard]] auto dump(AssetId id, std::string_view meshKey = {}) const -> std::expected<std::string, Error>;

    /// @brief The folder of the entry of 'id'.
    [[nodiscard]] auto entryDirectory(AssetId id) const -> std::filesystem::path;

    [[nodiscard]] auto directory() const noexcept -> const std::filesystem::path& { return m_directory; }

private:
    std::filesystem::path m_directory;
};
}
