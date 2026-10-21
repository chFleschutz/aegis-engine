export module aegis.assets:asset_id;
import std;

export namespace aegis::assets
{
/// @brief The identity of an asset, or of one part of a file (a glTF's mesh, material, texture): one random
///        64-bit value. Zero is "no asset"; generate() never returns it.
/// @note In files an ID is exactly 16 lowercase hex digits, leading zeros included (toString / parse); anything
///       else is rejected on read.
struct AssetId
{
    std::uint64_t value{ 0 };

    /// @brief A new random ID, from one std::mt19937_64 seeded once from std::random_device. Thread-safe.
    [[nodiscard]] static auto generate() -> AssetId;

    /// @brief Reads the file form: exactly 16 lowercase hex digits (and not all zero).
    [[nodiscard]] static auto parse(std::string_view text) -> std::optional<AssetId>;

    [[nodiscard]] auto valid() const noexcept -> bool { return value != 0; }
    [[nodiscard]] auto toString() const -> std::string;

    [[nodiscard]] friend auto operator==(AssetId, AssetId) -> bool = default;
    [[nodiscard]] friend auto operator<=>(AssetId, AssetId) = default;
};
}

export template<>
struct std::hash<aegis::assets::AssetId>
{
    // The ID is random already: it hashes as it is.
    [[nodiscard]] auto operator()(aegis::assets::AssetId id) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(id.value);
    }
};

export template<>
struct std::formatter<aegis::assets::AssetId> : std::formatter<std::string>
{
    auto format(aegis::assets::AssetId id, std::format_context& context) const
    {
        return std::formatter<std::string>::format(id.toString(), context);
    }
};
