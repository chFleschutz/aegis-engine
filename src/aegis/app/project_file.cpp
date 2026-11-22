module aegis.project_file;

import std;
import aegis.platform;
import aegis.reflect;
import aegis.serialize;

namespace aegis
{
namespace
{
using reflect::Node;
using serialize::ErrorCode;
using serialize::makeError;

template<typename T>
using Result = std::expected<T, serialize::Error>;

// Fails for a member of 'object' that is not in 'known'.
auto checkKeys(const Node& object, std::string_view where, std::initializer_list<std::string_view> known) -> Result<void>
{
    for (const auto& member : object.items)
    {
        if (std::ranges::find(known, member.key) == known.end())
            return makeError(ErrorCode::InvalidData, std::format("{}: unknown key '{}'", where, member.key), member.line);
    }
    return {};
}

auto expectKind(const Node& node, Node::Kind kind, std::string_view what) -> Result<void>
{
    if (node.kind != kind)
        return makeError(ErrorCode::InvalidData, std::format("{} must be {}, not {}", what, reflect::nodeKindName(kind), reflect::nodeKindName(node.kind)), node.line);
    return {};
}

auto readSource(const Node& node, std::string_view what) -> Result<platform::InputSource>
{
    if (auto kind = expectKind(node, Node::Kind::String, what); !kind)
        return std::unexpected{ std::move(kind.error()) };
    const auto source = platform::parseInputSource(node.text);
    if (!source)
        return makeError(ErrorCode::InvalidData, std::format("{}: unknown input '{}'", what, node.text), node.line);
    return *source;
}

auto readAxisBinding(const Node& node, std::string_view axis) -> Result<platform::AxisBinding>
{
    const auto what = std::format("a binding of the axis '{}'", axis);
    if (auto kind = expectKind(node, Node::Kind::Object, what); !kind)
        return std::unexpected{ std::move(kind.error()) };
    if (auto keys = checkKeys(node, what, { "negative", "positive", "axis" }); !keys)
        return std::unexpected{ std::move(keys.error()) };

    const auto* negative = node.find("negative");
    const auto* positive = node.find("positive");
    if (const auto* gamepadAxis = node.find("axis"))
    {
        if (negative || positive)
            return makeError(ErrorCode::InvalidData, std::format("{} has 'axis' or 'negative' and 'positive', not both", what), node.line);
        auto source = readSource(*gamepadAxis, what);
        if (!source)
            return std::unexpected{ std::move(source.error()) };
        if (!std::holds_alternative<platform::GamepadAxis>(*source))
            return makeError(ErrorCode::InvalidData, std::format("{}: '{}' is no gamepad axis", what, gamepadAxis->text), gamepadAxis->line);
        return std::get<platform::GamepadAxis>(*source);
    }
    if (!negative || !positive)
        return makeError(ErrorCode::InvalidData, std::format("{} needs 'axis', or 'negative' and 'positive'", what), node.line);
    auto negativeSource = readSource(*negative, what);
    if (!negativeSource)
        return std::unexpected{ std::move(negativeSource.error()) };
    auto positiveSource = readSource(*positive, what);
    if (!positiveSource)
        return std::unexpected{ std::move(positiveSource.error()) };
    return platform::AxisPair{ *negativeSource, *positiveSource };
}

auto readInput(const Node& input) -> Result<platform::InputMap>
{
    if (auto kind = expectKind(input, Node::Kind::Object, "'input'"); !kind)
        return std::unexpected{ std::move(kind.error()) };
    if (auto keys = checkKeys(input, "'input'", { "actions", "axes", "deadZone" }); !keys)
        return std::unexpected{ std::move(keys.error()) };

    platform::InputMap map;
    if (const auto* actions = input.find("actions"))
    {
        if (auto kind = expectKind(*actions, Node::Kind::Object, "'actions'"); !kind)
            return std::unexpected{ std::move(kind.error()) };
        for (const auto& action : actions->items)
        {
            const auto what = std::format("the action '{}'", action.key);
            if (auto kind = expectKind(action, Node::Kind::Array, what); !kind)
                return std::unexpected{ std::move(kind.error()) };
            std::vector<platform::InputSource> sources;
            for (const auto& item : action.items)
            {
                auto source = readSource(item, what);
                if (!source)
                    return std::unexpected{ std::move(source.error()) };
                sources.push_back(*source);
            }
            if (!map.addAction(action.key, std::move(sources)))
                return makeError(ErrorCode::InvalidData, std::format("the name '{}' is taken twice", action.key), action.line);
        }
    }
    if (const auto* axes = input.find("axes"))
    {
        if (auto kind = expectKind(*axes, Node::Kind::Object, "'axes'"); !kind)
            return std::unexpected{ std::move(kind.error()) };
        for (const auto& axis : axes->items)
        {
            if (auto kind = expectKind(axis, Node::Kind::Array, std::format("the axis '{}'", axis.key)); !kind)
                return std::unexpected{ std::move(kind.error()) };
            std::vector<platform::AxisBinding> bindings;
            for (const auto& item : axis.items)
            {
                auto binding = readAxisBinding(item, axis.key);
                if (!binding)
                    return std::unexpected{ std::move(binding.error()) };
                bindings.push_back(*binding);
            }
            if (!map.addAxis(axis.key, std::move(bindings)))
                return makeError(ErrorCode::InvalidData, std::format("the name '{}' is taken twice", axis.key), axis.line);
        }
    }
    if (const auto* deadZone = input.find("deadZone"))
    {
        const auto value = deadZone->number();
        if (!value || *value < 0.0 || *value >= 1.0)
            return makeError(ErrorCode::InvalidData, "'deadZone' must be a number from 0 up to 1", deadZone->line);
        map.setDeadZone(static_cast<float>(*value));
    }
    return map;
}
}

auto parseProjectFile(std::string_view text) -> std::expected<ProjectFile, serialize::Error>
{
    auto root = serialize::parseJson(text);
    if (!root)
        return std::unexpected{ std::move(root.error()) };
    if (auto kind = expectKind(*root, Node::Kind::Object, "the project file"); !kind)
        return std::unexpected{ std::move(kind.error()) };
    if (auto keys = checkKeys(*root, "the project file", { "input" }); !keys)
        return std::unexpected{ std::move(keys.error()) };

    ProjectFile file;
    if (const auto* input = root->find("input"))
    {
        auto map = readInput(*input);
        if (!map)
            return std::unexpected{ std::move(map.error()) };
        file.input = std::move(*map);
    }
    return file;
}

auto loadProjectFile(const std::filesystem::path& project) -> std::expected<ProjectFile, std::string>
{
    if (project.empty())
        return ProjectFile{};
    const auto path = project / PROJECT_FILE_NAME;
    std::error_code error;
    if (!std::filesystem::exists(path, error))
        return ProjectFile{};

    std::ifstream stream{ path, std::ios::binary };
    if (!stream)
        return std::unexpected{ std::format("'{}': cannot open it", path.string()) };
    const std::string text{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
    auto file = parseProjectFile(text);
    if (!file)
        return std::unexpected{ std::format("'{}': {}", path.string(), serialize::describe(file.error())) };
    return std::move(*file);
}
}
