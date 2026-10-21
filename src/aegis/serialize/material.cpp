module aegis.serialize;
import std;
import aegis.reflect;

namespace aegis::serialize
{
namespace
{
using reflect::Node;
using namespace detail;

constexpr std::string_view materialType{ "Material" };

// A number, an array of 2 to 4 numbers, or a texture ID.
auto checkValue(const Node& value, std::string_view name) -> std::expected<void, Error>
{
    switch (value.kind)
    {
    case Node::Kind::Number: return {};
    case Node::Kind::String:
    {
        auto id = parseId(value, std::format("the texture of '{}'", name));
        if (!id)
            return std::unexpected{ id.error() };
        return {};
    }
    case Node::Kind::Array:
    {
        const bool numbers = std::ranges::all_of(value.items, [](const Node& item) { return item.kind == Node::Kind::Number; });
        if (value.items.size() >= 2 && value.items.size() <= 4 && numbers)
            return {};
        break;
    }
    default: break;
    }
    return makeError(ErrorCode::InvalidValue, std::format("'{}' must be a number, an array of 2 to 4 numbers or a texture ID", name), value.line);
}
}

auto parseMaterial(std::string_view text) -> std::expected<MaterialFile, Error>
{
    static constexpr auto keys = std::to_array<std::string_view>({ "type", "id", "version", "template", "parameters" });

    auto root = parseJson(text);
    if (!root)
        return std::unexpected{ root.error() };
    auto header = readHeader(*root, materialType, materialVersion, keys);
    if (!header)
        return std::unexpected{ header.error() };

    auto templateNode = required(*root, "template", Node::Kind::String, "the material");
    if (!templateNode)
        return std::unexpected{ templateNode.error() };
    auto templateId = parseId(**templateNode, "\"template\"");
    if (!templateId)
        return std::unexpected{ templateId.error() };

    auto parameters = optional(*root, "parameters", Node::Kind::Object);
    if (!parameters)
        return std::unexpected{ parameters.error() };

    MaterialFile material{ .id = header->id, .templateId = *templateId };
    if (*parameters)
    {
        for (const Node& value : (*parameters)->items)
        {
            if (auto checked = checkValue(value, value.key); !checked)
                return std::unexpected{ checked.error() };
        }
        material.parameters = **parameters;
    }
    return material;
}

auto writeMaterial(const MaterialFile& material) -> std::string
{
    Node root = writeHeader(materialType, material.id, materialVersion);
    root.add("template", Node::makeString(formatId(material.templateId)));
    Node& parameters = root.add("parameters", Node::makeObject());
    for (const Node& value : material.parameters.items)
        parameters.push(value);
    std::ranges::sort(parameters.items, {}, &Node::key);
    return writeJson(root);
}
}
