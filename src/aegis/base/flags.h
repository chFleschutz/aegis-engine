#pragma once

// Opt-in bitwise operators for a flag enum. Use it inside the enum's own namespace (and inside the
// `export` block if the enum is exported), directly after the enum, so ADL finds the operators:
//
//     enum class ImageUsage : std::uint32_t { None = 0, Sampled = 1 << 0, Storage = 1 << 1 };
//     AEGIS_FLAGS(ImageUsage)
//
// Defines | & ^ ~ |= &= ^=. Needs `import std;` in the using translation unit. Query bits with
// `aegis::base::hasFlag` from the `aegis.base` module. It is a macro because ADL only looks in the
// enum's namespace and a module cannot export a macro.

// clang-format off
#define AEGIS_FLAGS(EnumType)                                                                      \
    [[nodiscard]] constexpr auto operator|(EnumType a, EnumType b) noexcept -> EnumType            \
    {                                                                                              \
        return static_cast<EnumType>(static_cast<std::underlying_type_t<EnumType>>(a) |            \
                                     static_cast<std::underlying_type_t<EnumType>>(b));            \
    }                                                                                              \
    [[nodiscard]] constexpr auto operator&(EnumType a, EnumType b) noexcept -> EnumType            \
    {                                                                                              \
        return static_cast<EnumType>(static_cast<std::underlying_type_t<EnumType>>(a) &            \
                                     static_cast<std::underlying_type_t<EnumType>>(b));            \
    }                                                                                              \
    [[nodiscard]] constexpr auto operator^(EnumType a, EnumType b) noexcept -> EnumType            \
    {                                                                                              \
        return static_cast<EnumType>(static_cast<std::underlying_type_t<EnumType>>(a) ^            \
                                     static_cast<std::underlying_type_t<EnumType>>(b));            \
    }                                                                                              \
    [[nodiscard]] constexpr auto operator~(EnumType a) noexcept -> EnumType                        \
    {                                                                                              \
        return static_cast<EnumType>(~static_cast<std::underlying_type_t<EnumType>>(a));           \
    }                                                                                              \
    constexpr auto operator|=(EnumType& a, EnumType b) noexcept -> EnumType&                       \
    {                                                                                              \
        return a = a | b;                                                                          \
    }                                                                                              \
    constexpr auto operator&=(EnumType& a, EnumType b) noexcept -> EnumType&                       \
    {                                                                                              \
        return a = a & b;                                                                          \
    }                                                                                              \
    constexpr auto operator^=(EnumType& a, EnumType b) noexcept -> EnumType&                       \
    {                                                                                              \
        return a = a ^ b;                                                                          \
    }
// clang-format on
