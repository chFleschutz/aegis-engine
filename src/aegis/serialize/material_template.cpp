module aegis.serialize;
import std;
import aegis.assets;
import aegis.math;
import aegis.reflect;

namespace aegis::serialize
{
namespace
{
using assets::ParameterType;
using reflect::Node;
using namespace detail;

constexpr std::string_view templateType{ "MaterialTemplate" };

auto isName(std::string_view text) -> bool
{
    return !text.empty() && std::ranges::all_of(text, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

auto parseType(const Node& node) -> std::expected<ParameterType, Error>
{
    constexpr std::array types{ ParameterType::Int, ParameterType::UInt, ParameterType::Float, ParameterType::Vec2,
        ParameterType::Vec3, ParameterType::Vec4, ParameterType::Texture };
    for (const auto type : types)
    {
        if (node.text == assets::parameterTypeName(type))
            return type;
    }
    return makeError(ErrorCode::InvalidValue,
        std::format("'{}' is not a parameter type (int, uint, float, vec2, vec3, vec4 or texture)", node.text), node.line);
}

// The numbers of a vector default: an array of exactly 'count' numbers.
template<std::size_t count>
auto parseVector(const Node& node, std::string_view name) -> std::expected<std::array<float, count>, Error>
{
    std::array<float, count> vector{};
    const bool numbers = std::ranges::all_of(node.items, [](const Node& item) { return item.number().has_value(); });
    if (node.kind != Node::Kind::Array || node.items.size() != count || !numbers)
        return makeError(ErrorCode::InvalidValue, std::format("the default of '{}' must be an array of {} numbers", name, count), node.line);
    for (std::size_t i = 0; i < count; ++i)
        vector[i] = static_cast<float>(*node.items[i].number());
    return vector;
}

// A whole number in [min, max].
auto parseWhole(const Node& node, std::string_view name, double min, double max) -> std::expected<double, Error>
{
    const auto value = node.number();
    if (!value || *value != std::trunc(*value) || *value < min || *value > max)
        return makeError(ErrorCode::InvalidValue, std::format("the default of '{}' must be a whole number in [{}, {}]", name, min, max), node.line);
    return *value;
}

auto parseDefault(const Node& node, ParameterType type, std::string_view name) -> std::expected<assets::ParameterDefault, Error>
{
    switch (type)
    {
    case ParameterType::Int:
    {
        auto value = parseWhole(node, name, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
        if (!value)
            return std::unexpected{ value.error() };
        return static_cast<std::int32_t>(*value);
    }
    case ParameterType::UInt:
    {
        auto value = parseWhole(node, name, 0.0, std::numeric_limits<std::uint32_t>::max());
        if (!value)
            return std::unexpected{ value.error() };
        return static_cast<std::uint32_t>(*value);
    }
    case ParameterType::Float:
    {
        const auto value = node.number();
        if (!value)
            return makeError(ErrorCode::InvalidValue, std::format("the default of '{}' must be a number", name), node.line);
        return static_cast<float>(*value);
    }
    case ParameterType::Vec2:
        return parseVector<2>(node, name).transform([](const auto& v) -> assets::ParameterDefault { return glm::vec2{ v[0], v[1] }; });
    case ParameterType::Vec3:
        return parseVector<3>(node, name).transform([](const auto& v) -> assets::ParameterDefault { return glm::vec3{ v[0], v[1], v[2] }; });
    case ParameterType::Vec4:
        return parseVector<4>(node, name).transform([](const auto& v) -> assets::ParameterDefault { return glm::vec4{ v[0], v[1], v[2], v[3] }; });
    case ParameterType::Texture:
        if (node.kind == Node::Kind::String)
        {
            if (node.text == "white")
                return assets::DefaultTexture::White;
            if (node.text == "black")
                return assets::DefaultTexture::Black;
            if (node.text == "normal")
                return assets::DefaultTexture::Normal;
        }
        return makeError(ErrorCode::InvalidValue, std::format("the default of '{}' must be \"white\", \"black\" or \"normal\"", name), node.line);
    }
    return makeError(ErrorCode::InvalidValue, std::format("'{}' has no type", name), node.line);
}

auto parseParameter(const Node& node) -> std::expected<assets::TemplateParameter, Error>
{
    static constexpr auto keys = std::to_array<std::string_view>({ "name", "type", "default" });
    if (auto checked = expectKind(node, Node::Kind::Object, "a parameter"); !checked)
        return std::unexpected{ checked.error() };
    if (auto checked = checkKeys(node, keys, "a parameter"); !checked)
        return std::unexpected{ checked.error() };

    auto name = required(node, "name", Node::Kind::String, "a parameter");
    if (!name)
        return std::unexpected{ name.error() };
    if (!isName((*name)->text))
        return makeError(ErrorCode::InvalidValue, std::format("'{}' is not a parameter name (letters, digits and underscores)", (*name)->text), (*name)->line);

    const auto what = std::format("parameter '{}'", (*name)->text);
    auto typeNode = required(node, "type", Node::Kind::String, what);
    if (!typeNode)
        return std::unexpected{ typeNode.error() };
    auto type = parseType(**typeNode);
    if (!type)
        return std::unexpected{ type.error() };

    const auto* defaultNode = node.find("default");
    if (!defaultNode)
        return makeError(ErrorCode::InvalidData, std::format("{} has no \"default\"", what), node.line);
    auto defaultValue = parseDefault(*defaultNode, *type, (*name)->text);
    if (!defaultValue)
        return std::unexpected{ defaultValue.error() };
    return assets::TemplateParameter{ .name = (*name)->text, .defaultValue = *defaultValue };
}

// The member 'key' of 'object' as a whole number of at least 0.
auto readSize(const Node& object, std::string_view key) -> std::optional<std::size_t>
{
    const auto* node = object.find(key);
    const auto value = node ? node->number() : std::nullopt;
    if (!value || *value < 0.0 || *value != std::trunc(*value))
        return std::nullopt;
    return static_cast<std::size_t>(*value);
}

// The type of a reflected field, if a parameter can have it: a 32-bit scalar, a float vector, or the texture
// descriptor struct (bindless::TextureDescriptor).
auto reflectedType(const Node& type) -> std::optional<ParameterType>
{
    const auto* kind = type.find("kind");
    if (!kind)
        return std::nullopt;
    if (kind->text == "scalar")
    {
        const auto* scalar = type.find("scalarType");
        if (scalar && scalar->text == "int32")
            return ParameterType::Int;
        if (scalar && scalar->text == "uint32")
            return ParameterType::UInt;
        if (scalar && scalar->text == "float32")
            return ParameterType::Float;
        return std::nullopt;
    }
    if (kind->text == "vector")
    {
        const auto* element = type.find("elementType");
        const auto* scalar = element ? element->find("scalarType") : nullptr;
        if (!scalar || scalar->text != "float32")
            return std::nullopt;
        switch (readSize(type, "elementCount").value_or(0))
        {
        case 2: return ParameterType::Vec2;
        case 3: return ParameterType::Vec3;
        case 4: return ParameterType::Vec4;
        default: return std::nullopt;
        }
    }
    if (kind->text == "struct")
    {
        const auto* name = type.find("name");
        if (name && name->text == "TextureDescriptor")
            return ParameterType::Texture;
    }
    return std::nullopt;
}

// The member of array 'items' whose "name" is 'name'.
auto findNamed(const Node* array, std::string_view name) -> const Node*
{
    if (!array || array->kind != Node::Kind::Array)
        return nullptr;
    for (const auto& item : array->items)
    {
        const auto* itemName = item.find("name");
        if (itemName && itemName->text == name)
            return &item;
    }
    return nullptr;
}
}

auto parseTemplate(std::string_view text) -> std::expected<TemplateFile, Error>
{
    static constexpr auto keys = std::to_array<std::string_view>({ "type", "id", "version", "surface", "parameters" });

    auto root = parseJson(text);
    if (!root)
        return std::unexpected{ root.error() };
    auto header = readHeader(*root, templateType, templateVersion, keys);
    if (!header)
        return std::unexpected{ header.error() };

    auto surface = required(*root, "surface", Node::Kind::String, "the material template");
    if (!surface)
        return std::unexpected{ surface.error() };
    if (!isName((*surface)->text))
        return makeError(ErrorCode::InvalidValue, std::format("'{}' is not a surface shader name (letters, digits and underscores)", (*surface)->text), (*surface)->line);

    auto parameters = required(*root, "parameters", Node::Kind::Array, "the material template");
    if (!parameters)
        return std::unexpected{ parameters.error() };

    TemplateFile file{ .id = header->id, .desc = { .surface = (*surface)->text } };
    for (const Node& node : (*parameters)->items)
    {
        auto parameter = parseParameter(node);
        if (!parameter)
            return std::unexpected{ parameter.error() };
        if (std::ranges::contains(file.desc.parameters, parameter->name, &assets::TemplateParameter::name))
            return makeError(ErrorCode::InvalidValue, std::format("parameter '{}' is listed twice", parameter->name), node.line);
        file.desc.parameters.push_back(std::move(*parameter));
    }
    return file;
}

auto parseSurfaceLayout(std::string_view text) -> std::expected<assets::SurfaceBlock, Error>
{
    auto root = parseJson(text);
    if (!root)
        return std::unexpected{ root.error() };

    // parameters[push].type.elementType.fields[params].type: the pointer to the block, whose valueType is its struct
    const auto* push = findNamed(root->find("parameters"), "push");
    const auto* constants = push ? push->find("type") : nullptr;
    const auto* constantsStruct = constants ? constants->find("elementType") : nullptr;
    const auto* params = constantsStruct ? findNamed(constantsStruct->find("fields"), "params") : nullptr;
    const auto* pointer = params ? params->find("type") : nullptr;
    const auto* block = pointer ? pointer->find("valueType") : nullptr;
    const auto* fields = block ? block->find("fields") : nullptr;
    const auto* sizes = block ? block->find("sizes") : nullptr;
    if (!fields || fields->kind != Node::Kind::Array || !sizes || sizes->kind != Node::Kind::Array || sizes->items.empty())
        return makeError(ErrorCode::InvalidData, "not a surface shader's layout: no parameter block behind push.params", root->line);
    const auto size = readSize(sizes->items.front(), "value");
    if (!size)
        return makeError(ErrorCode::InvalidData, "the parameter block has no size", sizes->line);

    assets::SurfaceBlock result{ .size = *size };
    for (const auto& field : fields->items)
    {
        const auto* name = field.find("name");
        const auto* type = field.find("type");
        const auto* binding = field.find("binding");
        const auto offset = binding ? readSize(*binding, "offset") : std::nullopt;
        const auto fieldSize = binding ? readSize(*binding, "size") : std::nullopt;
        if (!name || name->kind != Node::Kind::String || !type || !offset || !fieldSize)
            return makeError(ErrorCode::InvalidData, "a field of the parameter block has no name, type, offset or size", field.line);
        result.fields.push_back({ .name = name->text, .offset = *offset, .size = *fieldSize, .type = reflectedType(*type) });
    }
    return result;
}
}
