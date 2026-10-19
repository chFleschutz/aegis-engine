export module aegis.base:flags;

import std;

export namespace aegis::base
{
/// @brief An enum that opted in to bitwise operators with AEGIS_FLAGS (aegis/base/flags.h).
/// @note The operators are found by ADL, they live next to the enum. An enum without the opt-in
///       (a Format, a Key) has no `|`, so mixing up enumerators does not compile.
template<typename T>
concept FlagEnum = std::is_enum_v<T> && requires(T a) {
    { a | a } -> std::same_as<T>;
    { a & a } -> std::same_as<T>;
};

/// @brief True if every bit of 'flag' is set in 'value'.
template<FlagEnum T>
constexpr auto hasFlag(T value, T flag) -> bool
{
    return (value & flag) == flag;
}
}
