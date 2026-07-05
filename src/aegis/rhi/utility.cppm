module;
#include <cassert>
#include <iostream>
#include <type_traits>
#include <utility>
#include <variant>

export module aegis.rhi:utility;

export namespace aegis::rhi::utility
{
///////////////////
// RHI Utilities //
///////////////////

/// @brief Aligns 'size' to the next multiple of 'alignment'.
/// @note 'alignment' must be a power of 2 greater than 0.
auto alignTo(std::size_t size, std::size_t alignment) -> std::size_t
{
    assert(alignment > 0 && "Alignment must be greater than 0");
    assert((alignment & (alignment - 1)) == 0 && "Alignment must be a power of 2");
    return (size + alignment - 1) & ~(alignment - 1);
}

///////////////////
// Variant match //
///////////////////

// TODO: Put this in some core module
// TODO: Also look at utility match(val, [](){}, ...) function
template<typename... Ts>
struct overloads : Ts...
{
    using Ts::operator()...;
};

// Deduction guide only required pre C++20
template<typename... Ts>
overloads(Ts...) -> overloads<Ts...>;

template<typename V, typename... Ts>
auto match(V&& variant, Ts&&... funcs)
{
    return std::visit(overloads{ std::forward<Ts>(funcs)... }, std::forward<V>(variant));
}

// std::variant<bool, int, float> value{ 42 };
// match(value,
//     [](bool v) { std::cout << "bool\n"; },
//     [](int v) { std::cout << "int\n"; },
//     [](auto v) { std::cout << "everything else\n"; }
// );

///////////////
// Flag enum //
///////////////

// TODO: move the flag enum concept stuff to core or its own module
template<typename T>
constexpr auto isFlagEnum{ false };

template<typename T>
concept FlagEnum = std::is_enum_v<T> /*&& isFlagEnum<T>*/;
// TODO: Sometimes clangd doesnt recognize the isFlagEnum template specialization and flags stuff as
// errors that are no compile errors.

template<FlagEnum T>
constexpr auto operator|(T a, T b) -> T
{
    return static_cast<T>(std::to_underlying(a) | std::to_underlying(b));
}

template<FlagEnum T>
constexpr auto operator&(T a, T b) -> T
{
    return static_cast<T>(std::to_underlying(a) & std::to_underlying(b));
}

template<FlagEnum T>
constexpr auto operator~(T a) -> T
{
    return static_cast<T>(~std::to_underlying(a));
}

template<FlagEnum T>
constexpr auto operator|=(T& a, T b) -> T&
{
    return a = a | b;
}

template<FlagEnum T>
constexpr auto operator&=(T& a, T b) -> T&
{
    return a = a & b;
}

template<FlagEnum T>
constexpr auto hasFlag(T value, T flag) -> bool
{
    return (value & flag) == flag;
}
}
