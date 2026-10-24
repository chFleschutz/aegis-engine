export module aegis.editor.core:asset_browser;
import std;
import aegis.math;
import :picking;

export namespace aegis::editor
{
/// @brief An asset as the browser lists it: a plain copy of an asset database entry (assets::AssetEntry), so
///        editor-core needs no asset library.
struct AssetItem
{
    std::uint64_t id{ 0 };
    std::size_t root{ 0 }; ///< Index into the browser's roots
    std::string path;      ///< Of its file, relative to the root, with '/'
    std::string key;       ///< Sub-asset key; empty for the file's main asset
    std::string type;      ///< Asset type (`Texture`, `Scene`, ...)
};

/// @brief A root folder of the browser: the project's content or the engine content.
struct AssetRoot
{
    std::string name;
    bool readOnly{ false }; ///< Engine content: shown dimmed
};

/// @brief A file of the browser, with its main asset and its parts (sub-assets), as indices into AssetTree::items.
struct AssetFile
{
    std::size_t root{ 0 };
    std::string path;                ///< Relative to the root
    std::string name;                ///< The file name
    std::optional<std::size_t> main; ///< None for a file whose main asset is not in the database
    std::vector<std::size_t> parts;  ///< By key
};

/// @brief A folder of the browser, with its subfolders and files as indices into AssetTree::folders and files.
struct AssetFolder
{
    std::size_t root{ 0 };
    std::string path; ///< Relative to the root; empty for the root folder
    std::string name; ///< The folder's name; the root's for a root folder
    std::optional<std::size_t> parent;
    std::vector<std::size_t> folders; ///< By name
    std::vector<std::size_t> files;   ///< By name
};

/// @brief What the file list shows: a search (in file names and sub-asset keys, ignoring ASCII case) and an asset type.
struct AssetFilter
{
    std::string search;
    std::string type; ///< Empty for every type

    /// @brief Whether it hides anything: the list then searches the folder's subfolders too.
    [[nodiscard]] auto active() const -> bool { return !search.empty() || !type.empty(); }
    /// @brief Whether 'item' is of the type, and its file name or key holds the search.
    [[nodiscard]] auto matches(const AssetItem& item) const -> bool;
};

/// @brief One file of the list and the parts it shows.
struct AssetRow
{
    std::size_t file{ 0 };
    std::vector<std::size_t> parts; ///< Indices into AssetTree::items: the parts that match the filter
    bool mainMatches{ false };      ///< Whether the file's main asset matches it
};

/// @brief The folder tree over an asset database's entries: one root folder per database root, the folders of the
///        files' paths below it, and each file's main asset and parts. Rebuilt after a rescan.
class AssetTree
{
public:
    /// @brief The tree of 'items', whose roots index 'roots'.
    [[nodiscard]] static auto build(std::vector<AssetItem> items, std::vector<AssetRoot> roots) -> AssetTree;

    [[nodiscard]] auto items() const -> const std::vector<AssetItem>& { return m_items; }
    [[nodiscard]] auto files() const -> const std::vector<AssetFile>& { return m_files; }
    [[nodiscard]] auto folders() const -> const std::vector<AssetFolder>& { return m_folders; }
    [[nodiscard]] auto roots() const -> const std::vector<AssetRoot>& { return m_roots; }

    /// @brief The root folder of root 'root' (folder i is root i's).
    [[nodiscard]] auto rootFolder(std::size_t root) const -> std::size_t { return root; }
    /// @brief The folder at 'path' of root 'root', or none: keeps the open folder across a rebuild.
    [[nodiscard]] auto findFolder(std::size_t root, std::string_view path) const -> std::optional<std::size_t>;

    /// @brief Every asset type of the items, sorted: the type filter's choices.
    [[nodiscard]] auto types() const -> std::vector<std::string>;

    /// @brief The files to list for 'folder': without an active filter its files with all their parts; with one, the
    ///        files of it and its subfolders (depth first) whose main asset or a part matches, with those parts.
    [[nodiscard]] auto rows(std::size_t folder, const AssetFilter& filter) const -> std::vector<AssetRow>;

private:
    void collectRows(std::size_t folder, const AssetFilter& filter, std::vector<AssetRow>& rows) const;

    std::vector<AssetItem> m_items;
    std::vector<AssetFile> m_files;
    std::vector<AssetFolder> m_folders;
    std::vector<AssetRoot> m_roots;
};

/// @brief The ImGui drag and drop type of an asset, dragged from the browser.
inline constexpr const char* assetPayloadType = "AEGIS_ASSET";

/// @brief The payload of an asset drag: its ID and its type, so a target can refuse it before the drop. Plain bytes,
///        as ImGui copies them.
struct AssetPayload
{
    std::uint64_t id{ 0 };
    std::array<char, 56> type{}; ///< Null-terminated, cut to fit

    [[nodiscard]] auto assetType() const -> std::string_view;
};

[[nodiscard]] auto makeAssetPayload(std::uint64_t id, std::string_view type) -> AssetPayload;

/// @brief Whether an asset slot of 'slotType' (reflect::FieldInfo::assetType) takes the dragged asset.
[[nodiscard]] auto acceptsAsset(std::string_view slotType, const AssetPayload& payload) -> bool;

/// @brief Whether the dragged asset can be instantiated into the world: a scene, or a model (whose main asset is the
///        scene of its nodes).
[[nodiscard]] auto canInstantiate(const AssetPayload& payload) -> bool;

/// @brief Where an instance dropped into the viewport goes: where 'ray' meets the ground plane z = 0 in front of it
///        within 'maxDistance', else 'fallbackDistance' along it.
[[nodiscard]] auto dropPoint(const Ray& ray, float maxDistance, float fallbackDistance) -> glm::vec3;
}
