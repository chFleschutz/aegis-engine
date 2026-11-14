module;

#include <compare>

module aegis.assets;
import std;

namespace aegis::assets
{
auto AssetId::generate() -> AssetId
{
    static std::mutex mutex;
    static std::mt19937_64 engine{ std::random_device{}() | (static_cast<std::uint64_t>(std::random_device{}()) << 32) };

    const std::scoped_lock lock{ mutex };
    for (;;)
    {
        const auto value = engine();
        if (value != 0)
            return AssetId{ value };
    }
}

auto AssetId::parse(std::string_view text) -> std::optional<AssetId>
{
    if (text.size() != 16)
        return std::nullopt;

    std::uint64_t value = 0;
    for (const char c : text)
    {
        std::uint64_t digit;
        if (c >= '0' && c <= '9')
            digit = static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<std::uint64_t>(c - 'a' + 10);
        else
            return std::nullopt; // Uppercase, signs, whitespace, a 0x prefix
        value = (value << 4) | digit;
    }
    if (value == 0)
        return std::nullopt;
    return AssetId{ value };
}

auto AssetId::toString() const -> std::string
{
    return std::format("{:016x}", value);
}
}
