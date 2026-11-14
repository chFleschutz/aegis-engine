export module aegis.assets:import_cache;
import std;
import :error;
import :image_data;
import :model_data;
import :sidecar;
import :texture_data;

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
//   <key>/texture.ktx2              a texture entry: the texture blob (see :texture_blob) of an image, external or
//                                   embedded
//   <key>/used                      empty; its write time is when the entry was last used (see evict)
//   manifests/<source key>.json     the files the import of a source read besides the source (a glTF's buffers)
//
// The key is an XXH3-64 hash of the source and its dependencies, the import settings, the importer version and the
// platform. The dependencies are known only after an import, so a model's load looks up in two steps: the source key
// (the same hash without the dependencies) names the manifest, and the dependencies' contents then give the entry's
// key. A texture has no dependencies: its key is that of the image's encoded bytes and its TextureImport, so a model
// and its embedded images are entries of their own, and changing an image's settings makes that texture again only.
// An entry is written to a staging folder and renamed into place; one that exists is complete and never changes, so
// other processes never read a partly written entry.

export namespace aegis::assets
{
/// @brief The version of the model importer, part of every model's cache key: bump it when importing the same file
///        gives other data (a new vertex attribute, other meshlet limits), so every entry is made again.
inline constexpr std::uint32_t modelImporterVersion{ 2 };

/// @brief The version of the texture importer, part of every texture's cache key: bump it when the same image gives
///        other data (other mips, another format), so every texture entry is made again.
inline constexpr std::uint32_t textureImporterVersion{ 1 };

/// @brief The platform the cache's data is made for, part of every key.
#ifdef _WIN32
inline constexpr std::string_view importPlatform{ "windows" };
#else
inline constexpr std::string_view importPlatform{ "linux" };
#endif

/// @brief What a cache key is made from.
struct ImportCacheKeyDesc
{
    std::string_view importer; // "model", "texture" or "environment"
    std::uint32_t importerVersion{ 0 };
    ImportSettings settings;
    std::string_view platform{ importPlatform };
    /// @brief The source file's bytes, then each dependency's in order (ModelData::dependencies); none for a file
    ///        that does not exist.
    std::vector<std::optional<std::span<const std::byte>>> files;
};

/// @brief The cache key: an XXH3-64 hash of every field of 'desc', so changing any of them gives another key.
[[nodiscard]] auto importCacheKey(const ImportCacheKeyDesc& desc) -> std::uint64_t;

/// @brief How an image becomes a texture entry. With the image's encoded bytes, it makes the entry's key.
struct TextureImport
{
    std::string_view name; // Names the image in messages
    /// @brief LDR data is color data (sRGB) or linear (see textureSrgb); mips average it accordingly.
    bool srgb{ false };
    /// @brief An environment map (`.hdr`): RGBA32F with mip 0 only, which render_graph::Ibl turns into a cube.
    bool environment{ false };
};

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

    struct TextureLoad
    {
        TextureData texture;
        bool cached{ false }; // Read from the cache; false if the image was decoded
    };

    /// @brief The texture of the encoded image 'encoded' (PNG, JPEG, Radiance HDR, ...): read from the entry of its
    ///        key if there is one, else decoded (or made from 'decoded', the pixels an import already has), given a
    ///        full mip chain (an environment map keeps mip 0 only) and written to that entry. An entry that cannot be
    ///        read is made again; one that cannot be written is logged and the texture is returned.
    /// @return The errors of decodeImage and textureFromImage.
    [[nodiscard]] auto loadTexture(std::span<const std::byte> encoded, const TextureImport& import, const ImageData* decoded = nullptr) const
        -> std::expected<TextureLoad, Error>;

    /// @brief loadTexture of the image file 'source'.
    /// @return ErrorCode::FileNotFound if 'source' does not exist, or the errors of loadTexture.
    [[nodiscard]] auto loadTextureFile(const std::filesystem::path& source, const TextureImport& import) const
        -> std::expected<TextureLoad, Error>;

    /// @brief The folder of the texture entry of 'encoded' with 'import'; it may not exist.
    [[nodiscard]] auto textureEntryDirectory(std::span<const std::byte> encoded, const TextureImport& import) const -> std::filesystem::path;

    /// @brief The texture entry of 'encoded' with 'import' as text: the blob's file, key, format, extent, mips and
    ///        layers.
    /// @return ErrorCode::FileNotFound if there is no entry for the image as it is now, or the blob's read errors.
    [[nodiscard]] auto dumpTexture(std::span<const std::byte> encoded, const TextureImport& import) const -> std::expected<std::string, Error>;

    /// @brief The cache entry of model file 'source' with 'settings' as text: the entry's header, key and
    ///        dependencies, counts and a line per primitive and embedded image. With 'partKey' only that part in
    ///        detail: for a StaticMesh key (`StaticMesh/<mesh>/<primitive>`) the primitive's mesh blob, its header,
    ///        bounds, the first vertices and meshlets; for an embedded image's key (`Texture/<name>`) its texture
    ///        entry (see dumpTexture), read with 'partSettings', the image's settings in the model's sidecar.
    /// @return ErrorCode::FileNotFound if there is no entry for the file as it is now (it has not been loaded since
    ///         it changed or the cache was cleared), ErrorCode::InvalidArgument for a key no part has, or the
    ///         entry's read errors.
    [[nodiscard]] auto dump(const std::filesystem::path& source, const ImportSettings& settings, std::string_view partKey = {},
        const ImportSettings& partSettings = {}) const -> std::expected<std::string, Error>;

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
