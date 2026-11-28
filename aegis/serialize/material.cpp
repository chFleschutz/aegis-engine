module aegis.serialize;
import std;
import aegis.assets;
import aegis.reflect;
import aegis.rhi;

namespace aegis::serialize
{
namespace
{
using reflect::Node;
using namespace detail;

constexpr std::string_view materialType{ "Material" };

// The file's names of the alpha and cull modes; the first of each is the default, which the file leaves out.
constexpr auto alphaModes = std::to_array<std::pair<std::string_view, assets::AlphaMode>>({
    { "opaque", assets::AlphaMode::Opaque },
    { "mask", assets::AlphaMode::Mask },
    { "blend", assets::AlphaMode::Blend },
});
constexpr auto cullModes = std::to_array<std::pair<std::string_view, rhi::CullMode>>({
    { "back", rhi::CullMode::Back },
    { "none", rhi::CullMode::None },
    { "front", rhi::CullMode::Front },
});

// The mode the optional member `key` names, the default (the first of `modes`) without one.
template<typename Mode, std::size_t N>
auto readMode(const Node& root, std::string_view key, const std::array<std::pair<std::string_view, Mode>, N>& modes)
    -> std::expected<Mode, Error>
{
    auto node = optional(root, key, Node::Kind::String);
    if (!node)
        return std::unexpected{ node.error() };
    if (!*node)
        return modes.front().second;
    const auto found = std::ranges::find(modes, (*node)->text, &std::pair<std::string_view, Mode>::first);
    if (found != modes.end())
        return found->second;
    std::string names;
    for (const auto& [name, mode] : modes)
        names += std::format("{}\"{}\"", names.empty() ? "" : ", ", name);
    return makeError(ErrorCode::InvalidValue, std::format("'{}' must be one of {}", key, names), (*node)->line);
}

// Adds the member `key` for `mode` unless it is the default (the first of `modes`).
template<typename Mode, std::size_t N>
void writeMode(Node& root, std::string_view key, Mode mode, const std::array<std::pair<std::string_view, Mode>, N>& modes)
{
    if (mode == modes.front().second)
        return;
    const auto found = std::ranges::find(modes, mode, &std::pair<std::string_view, Mode>::second);
    if (found != modes.end())
        root.add(std::string{ key }, Node::makeString(std::string{ found->first }));
}

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

auto materialMigrations() -> const FileMigrations&
{
    static const FileMigrations migrations;
    return migrations;
}

auto parseMaterial(std::string_view text) -> std::expected<MaterialFile, Error>
{
    static constexpr auto keys = std::to_array<std::string_view>({ "type", "id", "version", "template", "alphaMode", "cullMode", "parameters" });

    auto root = parseJson(text);
    if (!root)
        return std::unexpected{ root.error() };
    auto header = readHeader(*root, materialType, materialVersion, keys, materialMigrations());
    if (!header)
        return std::unexpected{ header.error() };

    auto templateNode = required(*root, "template", Node::Kind::String, "the material");
    if (!templateNode)
        return std::unexpected{ templateNode.error() };
    auto templateId = parseId(**templateNode, "\"template\"");
    if (!templateId)
        return std::unexpected{ templateId.error() };

    auto alphaMode = readMode(*root, "alphaMode", alphaModes);
    if (!alphaMode)
        return std::unexpected{ alphaMode.error() };
    auto cullMode = readMode(*root, "cullMode", cullModes);
    if (!cullMode)
        return std::unexpected{ cullMode.error() };

    auto parameters = optional(*root, "parameters", Node::Kind::Object);
    if (!parameters)
        return std::unexpected{ parameters.error() };

    MaterialFile material{ .id = header->id, .templateId = *templateId, .alphaMode = *alphaMode, .cullMode = *cullMode };
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
    writeMode(root, "alphaMode", material.alphaMode, alphaModes);
    writeMode(root, "cullMode", material.cullMode, cullModes);
    Node& parameters = root.add("parameters", Node::makeObject());
    for (const Node& value : material.parameters.items)
        parameters.push(value);
    std::ranges::sort(parameters.items, {}, &Node::key);
    return writeJson(root);
}
}
