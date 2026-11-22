export module aegis.core:match;

import std;

export namespace aegis::core
{
/// @brief Overload set built from callables, e.g. for std::visit.
template<typename... Ts>
struct overloads : Ts...
{
    using Ts::operator()...;
};

/// @brief Calls the callable that fits the variant's active alternative.
/// @code
/// std::variant<bool, int, float> value{ 42 };
/// core::match(value,
///     [](bool v) { /* bool */ },
///     [](int v) { /* int */ },
///     [](auto v) { /* everything else */ });
/// @endcode
template<typename V, typename... Ts>
auto match(V&& variant, Ts&&... funcs)
{
    return std::visit(overloads{ std::forward<Ts>(funcs)... }, std::forward<V>(variant));
}
}
