module aegis.reflect;
import std;

namespace aegis::reflect
{
namespace
{
auto componentCount(FieldKind kind) -> std::size_t
{
    switch (kind)
    {
    case FieldKind::Vec2: return 2;
    case FieldKind::Vec3: return 3;
    case FieldKind::Vec4:
    case FieldKind::Quat: return 4;
    default: return 0;
    }
}

auto isNumeric(FieldKind kind) -> bool
{
    return kind == FieldKind::Int || kind == FieldKind::UInt || kind == FieldKind::Float || kind == FieldKind::Double;
}

auto wrongShape(std::string_view expected, const Node& node) -> std::unexpected<Error>
{
    return makeError(ErrorCode::TypeMismatch, std::format("expected {}, got {}", expected, nodeKindName(node.kind)));
}

// JSON has no infinity or NaN; to_chars writes them as "inf" and "nan".
auto finiteNumber(std::string text) -> std::expected<Node, Error>
{
    if (text.contains("inf") || text.contains("nan"))
        return makeError(ErrorCode::OutOfRange, std::format("{} is not a finite number", text));
    return Node::makeNumber(std::move(text));
}
}

auto Node::makeNumber(double value) -> Node
{
    std::array<char, 64> buffer{};
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return makeNumber(std::string{ buffer.data(), result.ptr });
}

auto Node::number() const -> std::optional<double>
{
    if (kind != Kind::Number)
        return std::nullopt;
    double value = 0.0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

auto Node::find(std::string_view memberKey) -> Node*
{
    return const_cast<Node*>(std::as_const(*this).find(memberKey));
}

auto Node::find(std::string_view memberKey) const -> const Node*
{
    if (kind != Kind::Object)
        return nullptr;
    auto it = std::ranges::find(items, memberKey, &Node::key);
    return it == items.end() ? nullptr : &*it;
}

auto Node::add(std::string memberKey, Node value) -> Node&
{
    value.key = std::move(memberKey);
    return items.emplace_back(std::move(value));
}

auto Node::set(std::string_view memberKey, Node value) -> Node&
{
    Node* member = find(memberKey);
    if (!member)
        return add(std::string{ memberKey }, std::move(value));
    value.key = std::move(member->key);
    *member = std::move(value);
    return *member;
}

auto Node::push(Node value) -> Node&
{
    return items.emplace_back(std::move(value));
}

auto Node::remove(std::string_view memberKey) -> bool
{
    if (kind != Kind::Object)
        return false;
    return std::erase_if(items, [memberKey](const Node& item) { return item.key == memberKey; }) > 0;
}

auto Node::rename(std::string_view from, std::string to) -> bool
{
    Node* member = find(from);
    if (!member)
        return false;
    member->key = std::move(to);
    return true;
}

auto toNode(FieldKind kind, const Value& value, std::span<const EnumEntry> enumEntries) -> std::expected<Node, Error>
{
    if (kind == FieldKind::AssetRef)
    {
        if (auto* id = std::get_if<std::uint64_t>(&value); id && *id == 0)
            return Node{};
    }

    auto text = toText(kind, value, enumEntries);
    if (!text)
        return std::unexpected{ text.error() };

    switch (kind)
    {
    case FieldKind::Bool: return Node::makeBool(*text == "true");
    case FieldKind::Int:
    case FieldKind::UInt:
    case FieldKind::Float:
    case FieldKind::Double: return finiteNumber(std::move(*text));
    case FieldKind::String:
    case FieldKind::Enum:
    case FieldKind::AssetRef: return Node::makeString(std::move(*text));
    case FieldKind::Vec2:
    case FieldKind::Vec3:
    case FieldKind::Vec4:
    case FieldKind::Quat:
    {
        // The text form is the components separated by spaces, in file order (a quaternion's is x y z w).
        Node array = Node::makeArray();
        for (auto component : std::views::split(std::string_view{ *text }, ' '))
        {
            auto number = finiteNumber(std::string{ std::string_view{ component } });
            if (!number)
                return std::unexpected{ number.error() };
            array.push(std::move(*number));
        }
        return array;
    }
    }
    return makeError(ErrorCode::TypeMismatch, "unknown field kind");
}

auto fromNode(FieldKind kind, const Node& node, std::span<const EnumEntry> enumEntries) -> std::expected<Value, Error>
{
    if (kind == FieldKind::Bool)
    {
        if (node.kind != Node::Kind::Bool)
            return wrongShape("true or false", node);
        return Value{ node.boolean };
    }

    if (isNumeric(kind))
    {
        if (node.kind != Node::Kind::Number)
            return wrongShape("a number", node);
        return fromText(kind, node.text, enumEntries);
    }

    if (const std::size_t count = componentCount(kind); count > 0)
    {
        const auto expected = std::format("an array of {} numbers", count);
        if (node.kind != Node::Kind::Array || node.items.size() != count)
            return wrongShape(expected, node);
        std::string text;
        for (const Node& item : node.items)
        {
            if (item.kind != Node::Kind::Number)
                return wrongShape(expected, item);
            if (!text.empty())
                text += ' ';
            text += item.text;
        }
        return fromText(kind, text, enumEntries);
    }

    if (kind == FieldKind::AssetRef && node.kind == Node::Kind::Null)
        return Value{ std::uint64_t{ 0 } };
    if (node.kind != Node::Kind::String)
        return wrongShape(kind == FieldKind::AssetRef ? "an asset ID or null" : "a string", node);
    if (kind == FieldKind::AssetRef && node.text.empty())
        return makeError(ErrorCode::ParseError, "an asset ID is 16 lowercase hex digits (null for none)");
    return fromText(kind, node.text, enumEntries);
}
}
