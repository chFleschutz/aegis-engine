module aegis.serialize;
import std;
import aegis.reflect;

namespace aegis::serialize::detail
{
using reflect::Node;

auto readHeader(const Node& root, std::string_view type, std::uint32_t current, std::span<const std::string_view> keys)
    -> std::expected<Header, Error>
{
    if (auto checked = expectKind(root, Node::Kind::Object, "the file"); !checked)
        return std::unexpected{ checked.error() };

    auto typeNode = required(root, "type", Node::Kind::String, "the file");
    if (!typeNode)
        return std::unexpected{ typeNode.error() };
    if ((*typeNode)->text != type)
        return makeError(ErrorCode::WrongType, std::format("the file is a '{}', not a '{}'", (*typeNode)->text, type), (*typeNode)->line);
    if (auto checked = checkKeys(root, keys, std::format("the {}", type)); !checked)
        return std::unexpected{ checked.error() };

    auto idNode = required(root, "id", Node::Kind::String, "the file");
    if (!idNode)
        return std::unexpected{ idNode.error() };
    auto id = parseId(**idNode, "the file's \"id\"");
    if (!id)
        return std::unexpected{ id.error() };

    auto versionNode = required(root, "version", Node::Kind::Number, "the file");
    if (!versionNode)
        return std::unexpected{ versionNode.error() };
    auto version = parseVersion(**versionNode, "\"version\"");
    if (!version)
        return std::unexpected{ version.error() };
    if (*version > current)
    {
        return makeError(ErrorCode::UnsupportedVersion, std::format("the {} is version {}, newer than this engine's {}", type, *version, current),
            (*versionNode)->line);
    }
    return Header{ .id = *id, .version = *version };
}

auto writeHeader(std::string_view type, std::uint64_t id, std::uint32_t version) -> Node
{
    Node root = Node::makeObject();
    root.add("type", Node::makeString(std::string{ type }));
    root.add("id", Node::makeString(formatId(id)));
    root.add("version", Node::makeNumber(std::to_string(version)));
    return root;
}

auto formatId(std::uint64_t id) -> std::string
{
    return std::format("{:016x}", id);
}

auto parseId(const Node& node, std::string_view what) -> std::expected<std::uint64_t, Error>
{
    if (auto checked = expectKind(node, Node::Kind::String, what); !checked)
        return std::unexpected{ checked.error() };
    const std::string& text = node.text;
    const bool hex = std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    if (text.size() != 16 || !hex)
        return makeError(ErrorCode::InvalidData, std::format("{} '{}' is not 16 lowercase hex digits", what, text), node.line);
    std::uint64_t id = 0;
    std::from_chars(text.data(), text.data() + text.size(), id, 16);
    if (id == 0)
        return makeError(ErrorCode::InvalidData, std::format("{} must not be zero", what), node.line);
    return id;
}

auto parseVersion(const Node& node, std::string_view what) -> std::expected<std::uint32_t, Error>
{
    if (auto checked = expectKind(node, Node::Kind::Number, what); !checked)
        return std::unexpected{ checked.error() };
    std::uint32_t version = 0;
    auto result = std::from_chars(node.text.data(), node.text.data() + node.text.size(), version);
    if (result.ec != std::errc{} || result.ptr != node.text.data() + node.text.size() || version == 0)
        return makeError(ErrorCode::InvalidData, std::format("{} must be a whole number of at least 1, not {}", what, node.text), node.line);
    return version;
}

auto expectKind(const Node& node, Node::Kind kind, std::string_view what) -> std::expected<void, Error>
{
    if (node.kind != kind)
        return makeError(ErrorCode::InvalidData, std::format("{} must be {}, not {}", what, reflect::nodeKindName(kind), reflect::nodeKindName(node.kind)), node.line);
    return {};
}

auto required(const Node& object, std::string_view key, Node::Kind kind, std::string_view what) -> std::expected<const Node*, Error>
{
    const Node* found = object.find(key);
    if (!found)
        return makeError(ErrorCode::InvalidData, std::format("{} has no \"{}\"", what, key), object.line);
    if (auto checked = expectKind(*found, kind, std::format("\"{}\"", key)); !checked)
        return std::unexpected{ checked.error() };
    return found;
}

auto optional(const Node& object, std::string_view key, Node::Kind kind) -> std::expected<const Node*, Error>
{
    const Node* found = object.find(key);
    if (found)
    {
        if (auto checked = expectKind(*found, kind, std::format("\"{}\"", key)); !checked)
            return std::unexpected{ checked.error() };
    }
    return found;
}

auto checkKeys(const Node& object, std::span<const std::string_view> known, std::string_view what) -> std::expected<void, Error>
{
    for (const Node& member : object.items)
    {
        if (!std::ranges::contains(known, std::string_view{ member.key }))
            return makeError(ErrorCode::InvalidData, std::format("{} has an unknown key \"{}\"", what, member.key), member.line);
    }
    return {};
}
}
