export module aegis.assets:asset_database;
import std;
import :asset_id;
import :error;
import :sidecar;

export namespace aegis::assets
{
/// @brief Where an asset is: its file and, for a part of a foreign file, the sub-asset key.
struct AssetEntry
{
    AssetId id;
    std::size_t root{ 0 };   // Index into AssetDatabase::roots()
    std::string path;        // Of the source (or native) file, relative to the root, with '/'
    std::string key;         // Sub-asset key (see SubAssetKeys); empty for the file's main asset
    std::string type;        // Asset type (`Texture`, `Scene`, ...): from the importer, the key prefix or the header
    ImportSettings settings; // A foreign file's main entry: its sidecar's settings; defaults otherwise

    [[nodiscard]] friend auto operator==(const AssetEntry&, const AssetEntry&) -> bool = default;
};

/// @brief The index of every asset in the project and the engine content: `ID → path (+ key)`, the one place
///        paths exist. Built by scanning the roots for sidecars (`<file>.<ext>.meta`) and native files
///        (`.scene`, `.material`, `.template`, whose header carries the ID).
///        Every entry has an asset type: a foreign file's main entry gets it from the sidecar's importer
///        (see typeOfImporter), a sub-asset from its key prefix and a native file from its header.
/// @note Paths are case-sensitive on every platform: two paths that differ only in case are an error, and so
///       is an ID that two files claim (a copied file brings its IDs along; the editor gives the copy new
///       ones). The index is cached in a file with each file's modification time and size, so a later scan
///       rereads only files that changed.
class AssetDatabase
{
public:
    struct Root
    {
        std::filesystem::path path;
        bool readOnly{ false }; // Engine content
    };

    struct ScanDesc
    {
        std::vector<Root> roots;
        /// @brief Where the index is kept (`<project>/.aegis/index`); empty for none.
        std::filesystem::path indexFile;
        /// @brief Writes a sidecar with new IDs for each foreign file of a writable root that has none (what
        ///        the editor does on import), picking the importer from the extension (see importerFor).
        bool createSidecars{ false };
    };

    /// @brief Extensions of native files, which carry their ID in a `{type, id, version}` header. A `.template`
    ///        names a material template (for now only the engine content's PBR template, built in code).
    static constexpr std::array nativeExtensions{ std::string_view{ ".scene" }, std::string_view{ ".material" }, std::string_view{ ".template" } };

    /// @return ErrorCode::FileNotFound for a missing root, ErrorCode::InvalidData for a duplicate ID, a
    ///         case-only path clash or an unreadable sidecar / header (the message names the file).
    [[nodiscard]] static auto scan(ScanDesc desc) -> std::expected<AssetDatabase, Error>;

    [[nodiscard]] auto find(AssetId id) const -> const AssetEntry*;

    /// @brief The ID of the asset made from 'path' (relative to root 'root') and 'key' ("" for the main one).
    [[nodiscard]] auto find(std::size_t root, std::string_view path, std::string_view key = {}) const -> std::optional<AssetId>;

    /// @brief Records the parts of the file at 'path' (relative to root 'root'), as a model import assigned them.
    /// @return ErrorCode::InvalidData if an ID is already used by another asset.
    [[nodiscard]] auto addSubAssets(std::size_t root, std::string_view path, std::span<const std::pair<std::string, AssetId>> parts)
        -> std::expected<void, Error>;

    /// @brief Adds the file at 'path' (relative to root 'root') that was written after the scan, a native file or
    ///        a foreign file with its sidecar, to the database and the index, replacing its entries if it was
    ///        already there. A foreign file without a sidecar gets one as in a scan with createSidecars.
    /// @return ErrorCode::FileNotFound if the file (or its sidecar) cannot be read, ErrorCode::InvalidData for a
    ///         file that is no asset, an ID another file has, a case-only path clash or an unreadable file.
    [[nodiscard]] auto addFile(std::size_t root, std::string_view path) -> std::expected<void, Error>;

    /// @brief Scans again with the scan's settings, picking up files changed since. On failure the database keeps
    ///        its entries.
    [[nodiscard]] auto rescan() -> std::expected<void, Error>;

    /// @brief The file of an entry on disk.
    [[nodiscard]] auto absolutePath(const AssetEntry& entry) const -> std::filesystem::path;

    [[nodiscard]] auto roots() const noexcept -> const std::vector<Root>& { return m_desc.roots; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_entries.size(); }

    /// @brief Entries in ID order.
    [[nodiscard]] auto entries() const noexcept -> const std::vector<AssetEntry>& { return m_entries; }

    /// @brief How many files the scan read (the rest came from the index).
    [[nodiscard]] auto filesRead() const noexcept -> std::size_t { return m_filesRead; }

private:
    // A sidecar or native file read into the database, as the index records it.
    struct IndexedFile
    {
        std::size_t root{ 0 };
        std::string path; // Relative to the root
        std::int64_t modified{ 0 };
        std::uint64_t size{ 0 };
        std::vector<AssetId> entries;
    };

    auto writeIndex() const -> void;

    ScanDesc m_desc;
    std::vector<AssetEntry> m_entries; // Sorted by ID
    std::vector<IndexedFile> m_files;  // Sorted by root and path
    std::size_t m_filesRead{ 0 };
};

/// @brief The default importer of a foreign file by extension ("model", "texture" or "environment"), or an
///        empty view for a file that is no asset source.
[[nodiscard]] auto importerFor(const std::filesystem::path& path) -> std::string_view;

/// @brief The asset type of a foreign file's main asset made by 'importer' ("model" makes a `Scene`), or an empty
///        view for an unknown importer.
[[nodiscard]] auto typeOfImporter(std::string_view importer) -> std::string_view;

/// @brief True if 'path' has a native extension (see AssetDatabase::nativeExtensions).
[[nodiscard]] auto isNativeFile(const std::filesystem::path& path) -> bool;
}

export namespace aegis::assets::detail
{
/// @brief The first two of 'paths' that differ only in case, or none. Paths are case-sensitive on every
///        platform, so such a pair is rejected (the files cannot both exist on Windows or macOS).
[[nodiscard]] auto findCaseClash(std::span<const std::string> paths) -> std::optional<std::pair<std::string, std::string>>;
}
