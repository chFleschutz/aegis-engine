export module aegis.reflect:value;
import std;
import aegis.math;
import :error;

export namespace aegis::reflect
{
// What a field holds. Every C++ member type maps to exactly one kind (see FieldConversion).
enum class FieldKind
{
    Bool,
    Int,  // Any signed integer type, carried as int64_t
    UInt, // Any unsigned integer type, carried as uint64_t
    Float,
    Double,
    String,
    Vec2,
    Vec3,
    Vec4,
    Quat,
    Enum,     // Carried as int64_t; the field's EnumEntry list names the valid values
    AssetRef, // An asset reference, carried as the asset's uint64_t ID (0 for none); the text form is 16 hex digits
};

[[nodiscard]] constexpr auto kindName(FieldKind kind) -> std::string_view
{
    switch (kind)
    {
    case FieldKind::Bool: return "Bool";
    case FieldKind::Int: return "Int";
    case FieldKind::UInt: return "UInt";
    case FieldKind::Float: return "Float";
    case FieldKind::Double: return "Double";
    case FieldKind::String: return "String";
    case FieldKind::Vec2: return "Vec2";
    case FieldKind::Vec3: return "Vec3";
    case FieldKind::Vec4: return "Vec4";
    case FieldKind::Quat: return "Quat";
    case FieldKind::Enum: return "Enum";
    case FieldKind::AssetRef: return "AssetRef";
    }
    return "Unknown";
}

// A field's value, type-erased. Int and Enum fields both use the int64_t alternative, UInt and AssetRef
// fields the uint64_t one.
using Value = std::variant<bool, std::int64_t, std::uint64_t, float, double, std::string, glm::vec2, glm::vec3, glm::vec4, glm::quat>;

// One named enumerator of an enum field.
struct EnumEntry
{
    std::string name;
    std::int64_t value;
};

// Text form of a value, the format a serializer or an editor's text box exchanges:
//   bool   true | false
//   ints   decimal
//   floats shortest text that parses back to the same number
//   string the string itself (escaping is the file format's job)
//   vecN   components separated by spaces: "1 2 3"
//   quat   "x y z w" (components separated by spaces)
//   enum   the enumerator's name
// `enumEntries` is only read for FieldKind::Enum.
[[nodiscard]] auto toText(FieldKind kind, const Value& value, std::span<const EnumEntry> enumEntries = {}) -> std::expected<std::string, Error>;
[[nodiscard]] auto fromText(FieldKind kind, std::string_view text, std::span<const EnumEntry> enumEntries = {}) -> std::expected<Value, Error>;
}
