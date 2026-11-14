export module aegis.assets:asset_key;
import std;

export namespace aegis::assets
{
/// @brief Names an asset by where it comes from: a source file and which part of it. One file can hold
///        several assets (a glTF's meshes, materials and images), so the file's path alone is not enough.
/// @note A plain value. 'path' is relative to the project's asset root, written with '/' and compared
///       case-sensitively on every platform, so a key means the same on Windows and Linux. 'subIndex' is the
///       position of the part among the parts of its kind in the file (a glTF's mesh 3 is {path, 3}); a
///       single-asset file (an OBJ, a PNG) uses 0. The key is only a name: the importer decides what kind of
///       asset it names, and asset IDs (D8) are expected to map to keys.
struct AssetKey
{
    std::string path;
    std::uint32_t subIndex{ 0 };

    [[nodiscard]] friend auto operator==(const AssetKey&, const AssetKey&) -> bool = default;
};
}

export template<>
struct std::hash<aegis::assets::AssetKey>
{
    [[nodiscard]] auto operator()(const aegis::assets::AssetKey& key) const noexcept -> std::size_t
    {
        // Boost's hash_combine on the two members.
        const auto path = std::hash<std::string>{}(key.path);
        return path ^ (std::hash<std::uint32_t>{}(key.subIndex) + 0x9e3779b97f4a7c15ULL + (path << 6) + (path >> 2));
    }
};
