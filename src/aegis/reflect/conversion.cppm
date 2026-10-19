export module aegis.reflect:conversion;
import std;
import aegis.math;
import :error;
import :value;

export namespace aegis::reflect
{
// Names the enumerators of an enum so it can be a field type. Specialize it for each enum:
//
//     template <> struct aegis::reflect::EnumNames<LightType>
//     {
//         static constexpr auto values = std::to_array<std::pair<std::string_view, LightType>>({
//             { "Point", LightType::Point },
//             { "Spot", LightType::Spot },
//         });
//     };
//
// The names are the save-file format, so they are written out rather than derived. An enum without a
// specialization is rejected at compile time when it is registered as a field.
template<class E>
struct EnumNames;

// The per-type conversion trait: how a C++ member type maps to a Value. A specialization provides
//
//     static constexpr FieldKind kind;
//     static auto toValue(const T&) -> Value;
//     static auto fromValue(const Value&) -> std::expected<T, Error>;
//     static auto enumEntries() -> std::vector<EnumEntry>;   // enums only
//
// fromValue is the checked direction: it returns TypeMismatch for a Value of the wrong kind and
// OutOfRange for one that does not fit (an int64 that overflows an int16, a non-enumerator).
// Supported out of the box: bool, every integer type, float, double, std::string, glm::vec2/3/4,
// glm::quat and enums with EnumNames. A game adds a type (say an AssetId) by specializing this trait,
// reusing one of the existing kinds. The primary template is undefined, so an unsupported member type
// is a compile error at the `.field(...)` call.
template<class T>
struct FieldConversion;

namespace detail
{
template<class T>
auto exactly(const Value& value, FieldKind kind) -> std::expected<T, Error>
{
    if (auto* held = std::get_if<T>(&value))
        return *held;
    return makeError(ErrorCode::TypeMismatch, std::format("expected a {} value", kindName(kind)));
}

// An integer Value (either alternative) narrowed to T, range-checked.
template<class T>
auto toInteger(const Value& value, FieldKind kind) -> std::expected<T, Error>
{
    auto narrow = [](auto number) -> std::expected<T, Error> {
        if (!std::in_range<T>(number))
            return makeError(ErrorCode::OutOfRange, std::format("{} does not fit the field's type", number));
        return static_cast<T>(number);
    };
    if (auto* held = std::get_if<std::int64_t>(&value))
        return narrow(*held);
    if (auto* held = std::get_if<std::uint64_t>(&value))
        return narrow(*held);
    return makeError(ErrorCode::TypeMismatch, std::format("expected a {} value", kindName(kind)));
}
}

template<>
struct FieldConversion<bool>
{
    static constexpr FieldKind kind = FieldKind::Bool;
    static auto toValue(bool v) -> Value { return v; }
    static auto fromValue(const Value& v) -> std::expected<bool, Error> { return detail::exactly<bool>(v, kind); }
};

template<class T>
    requires(std::integral<T> && !std::same_as<T, bool>)
struct FieldConversion<T>
{
    static constexpr FieldKind kind = std::signed_integral<T> ? FieldKind::Int : FieldKind::UInt;
    static auto toValue(T v) -> Value
    {
        if constexpr (std::signed_integral<T>)
            return static_cast<std::int64_t>(v);
        else
            return static_cast<std::uint64_t>(v);
    }
    static auto fromValue(const Value& v) -> std::expected<T, Error> { return detail::toInteger<T>(v, kind); }
};

template<>
struct FieldConversion<float>
{
    static constexpr FieldKind kind = FieldKind::Float;
    static auto toValue(float v) -> Value { return v; }
    static auto fromValue(const Value& v) -> std::expected<float, Error> { return detail::exactly<float>(v, kind); }
};

template<>
struct FieldConversion<double>
{
    static constexpr FieldKind kind = FieldKind::Double;
    static auto toValue(double v) -> Value { return v; }
    static auto fromValue(const Value& v) -> std::expected<double, Error> { return detail::exactly<double>(v, kind); }
};

template<>
struct FieldConversion<std::string>
{
    static constexpr FieldKind kind = FieldKind::String;
    static auto toValue(const std::string& v) -> Value { return v; }
    static auto fromValue(const Value& v) -> std::expected<std::string, Error> { return detail::exactly<std::string>(v, kind); }
};

template<>
struct FieldConversion<glm::vec2>
{
    static constexpr FieldKind kind = FieldKind::Vec2;
    static auto toValue(const glm::vec2& v) -> Value { return v; }
    static auto fromValue(const Value& v) -> std::expected<glm::vec2, Error> { return detail::exactly<glm::vec2>(v, kind); }
};

template<>
struct FieldConversion<glm::vec3>
{
    static constexpr FieldKind kind = FieldKind::Vec3;
    static auto toValue(const glm::vec3& v) -> Value { return v; }
    static auto fromValue(const Value& v) -> std::expected<glm::vec3, Error> { return detail::exactly<glm::vec3>(v, kind); }
};

template<>
struct FieldConversion<glm::vec4>
{
    static constexpr FieldKind kind = FieldKind::Vec4;
    static auto toValue(const glm::vec4& v) -> Value { return v; }
    static auto fromValue(const Value& v) -> std::expected<glm::vec4, Error> { return detail::exactly<glm::vec4>(v, kind); }
};

template<>
struct FieldConversion<glm::quat>
{
    static constexpr FieldKind kind = FieldKind::Quat;
    static auto toValue(const glm::quat& v) -> Value { return v; }
    static auto fromValue(const Value& v) -> std::expected<glm::quat, Error> { return detail::exactly<glm::quat>(v, kind); }
};

// An enum travels as the int64 of its value; EnumNames lists the valid ones.
template<class E>
    requires std::is_enum_v<E>
struct FieldConversion<E>
{
    static constexpr FieldKind kind = FieldKind::Enum;
    static auto toValue(E v) -> Value { return static_cast<std::int64_t>(std::to_underlying(v)); }
    static auto fromValue(const Value& v) -> std::expected<E, Error>
    {
        auto number = detail::toInteger<std::underlying_type_t<E>>(v, kind);
        if (!number)
            return std::unexpected{ number.error() };
        for (const auto& [name, value] : EnumNames<E>::values)
        {
            if (std::to_underlying(value) == *number)
                return value;
        }
        return makeError(ErrorCode::OutOfRange, std::format("{} is not an enumerator", *number));
    }
    static auto enumEntries() -> std::vector<EnumEntry>
    {
        std::vector<EnumEntry> entries;
        for (const auto& [name, value] : EnumNames<E>::values)
            entries.push_back({ .name = std::string{ name }, .value = static_cast<std::int64_t>(std::to_underlying(value)) });
        return entries;
    }
};
}
