export module aegis.assets:import_cache;
import std;
import :error;
import :model_data;
import :sidecar;

// The import cache (docs/asset-system.md, "Import cache and cooking"): what importing a source file produced, kept
// in the project's `.aegis/cache/imported/` so a later load reads it instead of importing again. Never committed
// and safe to delete. One folder per import result, named by its key, so identical inputs share one entry wherever
// the file is and whichever asset it is, and switching a file back to an earlier version finds that version's entry:
//
//   <key>/model.json                the model entry: its key, the files it depends on, the node tree, materials,
//                                   images and the blob of each primitive
//   <key>/mesh-<m>-<p>.mesh         the mesh blob (see :mesh_blob) of primitive p of mesh m
//   <key>/image-<i>.bin             the encoded bytes of embedded image i; an external image is an asset of its own
//                                   and only its URI is kept
//   <key>/used                      empty; its write time is when the entry was last used (see evict)
//   manifests/<source key>.json     the files the import of a source read besides the source (a glTF's buffers)
//
// The key is an XXH3-64 hash of the source and its dependencies, the import settings, the importer version and the
// platform. The dependencies are known only after an import, so a load looks up in two steps: the source key (the
// same hash without the dependencies) names the manifest, and the dependencies' contents then give the entry's key.
// An entry is written to a staging folder and renamed into place; one that exists is complete and never changes, so
// other processes never read a partly written entry.

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
    /// @param directory The cache's folder (`<project>/.aegis/cache/imported`); empty for none, which always imports.
    explicit ImportCache(std::filesystem::path directory = {}) :
        m_directory{ std::move(directory) }
    {
    }

    struct ModelLoad
    {
        ModelData model;
        bool cached{ false }; // Read from the cache; false if the file was imported
    };

    /// @brief The model file 'source', imported with 'settings': read from the entry of its current key if there is
    ///        one, else imported (importModel) and written to that entry. An entry that cannot be read is imported
    ///        again; one that cannot be written is logged and the import is returned.
    /// @return The errors of importModel.
    [[nodiscard]] auto loadModel(const std::filesystem::path& source, const ImportSettings& settings) const
        -> std::expected<ModelLoad, Error>;

    /// @brief The cache entry of model file 'source' with 'settings' as text: the entry's header, key and
    ///        dependencies, counts and a line per primitive. With 'meshKey' (a StaticMesh sub-asset key,
    ///        `StaticMesh/<mesh>/<primitive>`) only that primitive's mesh blob, in detail: its header, bounds, the
    ///        first vertices and meshlets.
    /// @return ErrorCode::FileNotFound if there is no entry for the file as it is now (it has not been loaded since
    ///         it changed or the cache was cleared), ErrorCode::InvalidArgument for a key no primitive has, or the
    ///         entry's read errors.
    [[nodiscard]] auto dump(const std::filesystem::path& source, const ImportSettings& settings, std::string_view meshKey = {}) const
        -> std::expected<std::string, Error>;

    /// @brief The folder of the entry model file 'source' with 'settings' has now, found through its manifest; it
    ///        may not exist.
    /// @return ErrorCode::FileNotFound if the source or its manifest does not exist.
    [[nodiscard]] auto entryDirectory(const std::filesystem::path& source, const ImportSettings& settings) const
        -> std::expected<std::filesystem::path, Error>;

    struct EvictDesc
    {
        /// @brief Entries unused for longer are deleted.
        std::chrono::hours maxAge{ 24 * 30 };
        /// @brief Then the least recently used entries are deleted until the rest fit.
        std::uint64_t maxBytes{ 8ull << 30 };
        std::filesystem::file_time_type now{ std::filesystem::file_time_type::clock::now() };
    };

    struct EvictResult
    {
        std::size_t removed{ 0 }; // Entries deleted
        std::size_t kept{ 0 };    // Entries left, including those that could not be deleted
        std::uint64_t bytes{ 0 }; // The size of the entries left
    };

    /// @brief Deletes the entries 'desc' names, the manifests unused for its maxAge and what an interrupted write or
    ///        delete left. An entry that another process has open is skipped, never deleted in part.
    auto evict(const EvictDesc& desc) const -> EvictResult;

    /// @brief evict with the default EvictDesc: 30 days, 8 GiB.
    auto evict() const -> EvictResult;

    [[nodiscard]] auto directory() const noexcept -> const std::filesystem::path& { return m_directory; }

private:
    std::filesystem::path m_directory;
};
}
