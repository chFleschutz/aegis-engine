export module aegis.core:text;

import std;

export namespace aegis::core
{
/// @brief Whether 'text' holds 'part', ignoring ASCII case; any text holds the empty part.
/// @note The search boxes of the editor (assets, console, hierarchy) use it.
[[nodiscard]] constexpr auto containsIgnoringCase(std::string_view text, std::string_view part) -> bool
{
    constexpr auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
    return part.empty() || !std::ranges::search(text, part, [&](char a, char b) { return lower(a) == lower(b); }).empty();
}
}
