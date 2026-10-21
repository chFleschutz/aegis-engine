export module aegis.assets:sub_asset_key;
import std;
import :error;

export namespace aegis::assets
{
/// @brief Escapes '\', '/' and '#' in a name with '\', so a name never reads as key structure.
[[nodiscard]] auto escapeKeyName(std::string_view name) -> std::string;

/// @brief Builds the sub-asset keys of one file: `<Type>/<name>`, plus `/<index>` for parts that are
///        positions inside another part (a glTF primitive: `StaticMesh/<mesh name>/<primitive index>`).
///        Keys match parts across reimports, so they come from the file's names, never bad enough to
///        reject the file: a duplicate name gets `#<n>` by order of appearance (`Cube`, `Cube#1`), an unnamed
///        part is `#<position>` among its type (`Texture/#3`), and both warn that references follow file
///        order until the parts are renamed. With 'strictNames' they are errors instead.
class SubAssetKeys
{
public:
    explicit SubAssetKeys(bool strictNames = false) :
        m_strictNames{ strictNames }
    {
    }

    /// @brief The key for the part 'position' (its order among the file's parts of 'type') called 'name'
    ///        (empty if unnamed). Call once per part; a part with several sub-parts adds them through
    ///        withIndex. Warnings go to aegis::log::warn and warnings().
    [[nodiscard]] auto name(std::string_view type, std::string_view name, std::size_t position)
        -> std::expected<std::string, Error>;

    /// @brief 'key' + "/<index>", for a part that is a position inside another part.
    [[nodiscard]] static auto withIndex(std::string_view key, std::size_t index) -> std::string;

    /// @brief The key of the material a model's primitives without one share (no part of the file, but
    ///        referenced like one). An unescaped '#' that is no position, so no material name can produce it.
    static constexpr std::string_view defaultMaterial{ "Material/#default" };

    /// @brief The type of the keys of a model's entities (`Entity/<node name>`): the local entity IDs of the
    ///        model's scene, kept in the sidecar like the parts' IDs so they survive reimports. Entities are no
    ///        assets, so the database leaves these keys out.
    static constexpr std::string_view entityType{ "Entity" };

    /// @brief The key of a model's root entity, which no node name can produce (see defaultMaterial).
    static constexpr std::string_view rootEntity{ "Entity/#root" };

    /// @brief True for the key of a model's entity (see entityType).
    [[nodiscard]] static auto isEntityKey(std::string_view key) -> bool;

    /// @brief The messages of the warnings so far.
    [[nodiscard]] auto warnings() const noexcept -> const std::vector<std::string>& { return m_warnings; }

private:
    bool m_strictNames;
    std::unordered_map<std::string, std::size_t> m_seen; // Escaped "<Type>/<name>" -> times used
    std::vector<std::string> m_warnings;
};
}
