module aegis.reflect;
import std;
import aegis.math;

namespace aegis::reflect
{
namespace
{
auto mismatch(FieldKind kind) -> std::unexpected<Error>
{
    return makeError(ErrorCode::TypeMismatch, std::format("the value is not a {}", kindName(kind)));
}

template<class T>
auto numberToText(T number) -> std::string
{
    std::array<char, 64> buffer{};
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    return std::string{ buffer.data(), result.ptr };
}

template<class T>
auto numberFromText(std::string_view text) -> std::expected<T, Error>
{
    T number{};
    auto result = std::from_chars(text.data(), text.data() + text.size(), number);
    if (result.ec == std::errc::result_out_of_range)
        return makeError(ErrorCode::OutOfRange, std::format("'{}' is out of range", text));
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return makeError(ErrorCode::ParseError, std::format("'{}' is not a number", text));
    return number;
}

// Splits "a b c" on spaces into exactly `count` components.
template<std::size_t N>
auto parseComponents(std::string_view text) -> std::expected<std::array<float, N>, Error>
{
    std::array<float, N> components{};
    std::size_t count = 0;
    while (!text.empty())
    {
        auto start = text.find_first_not_of(' ');
        if (start == std::string_view::npos)
            break;
        text.remove_prefix(start);
        auto end = std::min(text.find(' '), text.size());
        if (count == N)
            return makeError(ErrorCode::ParseError, std::format("expected {} components, got more", N));
        auto number = numberFromText<float>(text.substr(0, end));
        if (!number)
            return std::unexpected{ number.error() };
        components[count++] = *number;
        text.remove_prefix(end);
    }
    if (count != N)
        return makeError(ErrorCode::ParseError, std::format("expected {} components, got {}", N, count));
    return components;
}

auto joinComponents(std::span<const float> components) -> std::string
{
    std::string text;
    for (float component : components)
    {
        if (!text.empty())
            text += ' ';
        text += numberToText(component);
    }
    return text;
}
}

auto toText(FieldKind kind, const Value& value, std::span<const EnumEntry> enumEntries) -> std::expected<std::string, Error>
{
    switch (kind)
    {
    case FieldKind::Bool:
        if (auto* v = std::get_if<bool>(&value))
            return *v ? "true" : "false";
        return mismatch(kind);
    case FieldKind::Int:
        if (auto* v = std::get_if<std::int64_t>(&value))
            return numberToText(*v);
        return mismatch(kind);
    case FieldKind::UInt:
        if (auto* v = std::get_if<std::uint64_t>(&value))
            return numberToText(*v);
        return mismatch(kind);
    case FieldKind::Float:
        if (auto* v = std::get_if<float>(&value))
            return numberToText(*v);
        return mismatch(kind);
    case FieldKind::Double:
        if (auto* v = std::get_if<double>(&value))
            return numberToText(*v);
        return mismatch(kind);
    case FieldKind::String:
        if (auto* v = std::get_if<std::string>(&value))
            return *v;
        return mismatch(kind);
    case FieldKind::Vec2:
        if (auto* v = std::get_if<glm::vec2>(&value))
            return joinComponents(std::to_array({ v->x, v->y }));
        return mismatch(kind);
    case FieldKind::Vec3:
        if (auto* v = std::get_if<glm::vec3>(&value))
            return joinComponents(std::to_array({ v->x, v->y, v->z }));
        return mismatch(kind);
    case FieldKind::Vec4:
        if (auto* v = std::get_if<glm::vec4>(&value))
            return joinComponents(std::to_array({ v->x, v->y, v->z, v->w }));
        return mismatch(kind);
    case FieldKind::Quat:
        if (auto* v = std::get_if<glm::quat>(&value))
            return joinComponents(std::to_array({ v->x, v->y, v->z, v->w }));
        return mismatch(kind);
    case FieldKind::AssetRef:
        if (auto* v = std::get_if<std::uint64_t>(&value))
            return *v == 0 ? std::string{} : std::format("{:016x}", *v);
        return mismatch(kind);
    case FieldKind::Enum:
        if (auto* v = std::get_if<std::int64_t>(&value))
        {
            for (const auto& entry : enumEntries)
            {
                if (entry.value == *v)
                    return entry.name;
            }
            return makeError(ErrorCode::OutOfRange, std::format("{} is not an enumerator", *v));
        }
        return mismatch(kind);
    }
    return makeError(ErrorCode::TypeMismatch, "unknown field kind");
}

auto fromText(FieldKind kind, std::string_view text, std::span<const EnumEntry> enumEntries) -> std::expected<Value, Error>
{
    // Wraps a parsed number (or any expected<T>) into the Value variant.
    auto wrap = [](auto&& parsed) -> std::expected<Value, Error> {
        if (!parsed)
            return std::unexpected{ parsed.error() };
        return Value{ *parsed };
    };

    switch (kind)
    {
    case FieldKind::Bool:
        if (text == "true")
            return Value{ true };
        if (text == "false")
            return Value{ false };
        return makeError(ErrorCode::ParseError, std::format("'{}' is not true or false", text));
    case FieldKind::Int: return wrap(numberFromText<std::int64_t>(text));
    case FieldKind::UInt: return wrap(numberFromText<std::uint64_t>(text));
    case FieldKind::Float: return wrap(numberFromText<float>(text));
    case FieldKind::Double: return wrap(numberFromText<double>(text));
    case FieldKind::String: return Value{ std::string{ text } };
    case FieldKind::Vec2:
    {
        auto c = parseComponents<2>(text);
        return c ? std::expected<Value, Error>{ glm::vec2{ (*c)[0], (*c)[1] } } : std::unexpected{ c.error() };
    }
    case FieldKind::Vec3:
    {
        auto c = parseComponents<3>(text);
        return c ? std::expected<Value, Error>{ glm::vec3{ (*c)[0], (*c)[1], (*c)[2] } } : std::unexpected{ c.error() };
    }
    case FieldKind::Vec4:
    {
        auto c = parseComponents<4>(text);
        return c ? std::expected<Value, Error>{ glm::vec4{ (*c)[0], (*c)[1], (*c)[2], (*c)[3] } } : std::unexpected{ c.error() };
    }
    case FieldKind::Quat:
    {
        auto c = parseComponents<4>(text);
        // glm::quat's constructor takes (w, x, y, z); the text is "x y z w".
        return c ? std::expected<Value, Error>{ glm::quat{ (*c)[3], (*c)[0], (*c)[1], (*c)[2] } } : std::unexpected{ c.error() };
    }
    case FieldKind::AssetRef:
    {
        if (text.empty())
            return Value{ std::uint64_t{ 0 } };
        // Exactly 16 lowercase hex digits, as asset IDs are written in files.
        const bool hex = std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
        if (text.size() != 16 || !hex)
            return makeError(ErrorCode::ParseError, std::format("'{}' is not an asset ID (16 lowercase hex digits)", text));
        std::uint64_t id = 0;
        std::from_chars(text.data(), text.data() + text.size(), id, 16);
        return Value{ id };
    }
    case FieldKind::Enum:
        for (const auto& entry : enumEntries)
        {
            if (entry.name == text)
                return Value{ entry.value };
        }
        return makeError(ErrorCode::ParseError, std::format("'{}' is not an enumerator", text));
    }
    return makeError(ErrorCode::ParseError, "unknown field kind");
}
}
