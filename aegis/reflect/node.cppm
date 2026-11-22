export module aegis.reflect:node;
import std;
import :error;
import :value;

export namespace aegis::reflect
{
// A generic tree of values, shaped like JSON: what a serializer reads a file into and writes it from, and
// what a migration edits. Numbers keep their text, so a float written as its shortest round-trip text reads
// back as the same float (a detour through double could round it differently).
struct Node
{
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Kind kind{ Kind::Null };
    std::string key;         // An object member's key; empty for anything else
    std::string text;        // A number's text or a string's (decoded) characters
    bool boolean{ false };   // A Bool's value
    std::vector<Node> items; // An array's elements or an object's members, in order
    std::uint32_t line{ 0 }; // The 1-based line it starts on in the text it was read from; 0 if made in code

    [[nodiscard]] static auto makeBool(bool value) -> Node { return Node{ .kind = Kind::Bool, .boolean = value }; }
    [[nodiscard]] static auto makeNumber(std::string text) -> Node { return Node{ .kind = Kind::Number, .text = std::move(text) }; }
    [[nodiscard]] static auto makeNumber(double value) -> Node;
    [[nodiscard]] static auto makeString(std::string text) -> Node { return Node{ .kind = Kind::String, .text = std::move(text) }; }
    [[nodiscard]] static auto makeArray() -> Node { return Node{ .kind = Kind::Array }; }
    [[nodiscard]] static auto makeObject() -> Node { return Node{ .kind = Kind::Object }; }

    [[nodiscard]] auto isNull() const -> bool { return kind == Kind::Null; }
    [[nodiscard]] auto isObject() const -> bool { return kind == Kind::Object; }
    [[nodiscard]] auto isArray() const -> bool { return kind == Kind::Array; }

    // The value of a Number node, or none for another kind or text that is not a number.
    [[nodiscard]] auto number() const -> std::optional<double>;

    // An object's member, or null (also for anything that is not an object).
    [[nodiscard]] auto find(std::string_view memberKey) -> Node*;
    [[nodiscard]] auto find(std::string_view memberKey) const -> const Node*;

    // Appends a member to an object (no check for an existing key) and returns it.
    auto add(std::string memberKey, Node value) -> Node&;

    // Replaces an object's member, or appends it if there is none, and returns it. Use it rather than
    // assigning to a member found with find: the assignment would also replace the member's key.
    auto set(std::string_view memberKey, Node value) -> Node&;

    // Appends an element to an array and returns it.
    auto push(Node value) -> Node&;

    // Removes an object's member; false if there was none.
    auto remove(std::string_view memberKey) -> bool;

    // Renames an object's member; false if there was none.
    auto rename(std::string_view from, std::string to) -> bool;
};

[[nodiscard]] constexpr auto nodeKindName(Node::Kind kind) -> std::string_view
{
    switch (kind)
    {
    case Node::Kind::Null: return "null";
    case Node::Kind::Bool: return "a boolean";
    case Node::Kind::Number: return "a number";
    case Node::Kind::String: return "a string";
    case Node::Kind::Array: return "an array";
    case Node::Kind::Object: return "an object";
    }
    return "unknown";
}

// The node a field's value is written as:
//   Bool                     true | false
//   Int, UInt, Float, Double a number (floats as the shortest text that reads back the same)
//   String                   a string
//   Vec2/3/4                 an array of 2/3/4 numbers
//   Quat                     an array of 4 numbers, [x, y, z, w]
//   Enum                     the enumerator's name, a string
//   AssetRef                 the ID as 16 lowercase hex digits, a string; null for none
// `enumEntries` is only read for FieldKind::Enum.
[[nodiscard]] auto toNode(FieldKind kind, const Value& value, std::span<const EnumEntry> enumEntries = {}) -> std::expected<Node, Error>;

// The reverse: TypeMismatch for a node of the wrong shape, ParseError / OutOfRange for a bad value.
[[nodiscard]] auto fromNode(FieldKind kind, const Node& node, std::span<const EnumEntry> enumEntries = {}) -> std::expected<Value, Error>;
}
