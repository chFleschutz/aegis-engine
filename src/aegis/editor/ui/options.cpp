module aegis.editor;
import std;
import aegis.engine;

namespace aegis::editor
{
namespace
{
auto invalid(std::string message) -> std::unexpected<EngineError>
{
    return std::unexpected{ EngineError{ .code = EngineErrorCode::InvalidArguments, .message = std::move(message) } };
}
}

auto parseOptions(std::span<const ShellOption> options) -> std::expected<EditorOptions, EngineError>
{
    EditorOptions parsed;
    for (const auto& option : options)
    {
        const auto& name = option.name;
        if (name == "--editor-panels" || name == "--editor-select" || name == "--editor-stop")
        {
            if (!option.value)
                return invalid(std::format("{} needs a value", name));
            if (name == "--editor-select")
                parsed.select = *option.value;
            else if (name == "--editor-stop")
            {
                const auto& text = *option.value;
                int frames{ 0 };
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), frames);
                if (error != std::errc{} || end != text.data() + text.size() || frames < 1)
                    return invalid(std::format("--editor-stop takes a number of frames of at least 1, got '{}'", text));
                parsed.stopAfter = frames;
            }
            else if (*option.value == "all")
                parsed.allPanels = true;
            else
                return invalid(std::format("--editor-panels takes 'all', got '{}'", *option.value));
        }
        else if (name == "--editor-play" || name == "--editor-full-viewport" || name == "--editor-fold" || name == "--editor-check-stats")
        {
            if (option.value)
                return invalid(std::format("{} takes no value, got '{}'", name, *option.value));
            auto& flag = name == "--editor-play"            ? parsed.play
                         : name == "--editor-full-viewport" ? parsed.fullViewport
                         : name == "--editor-fold"          ? parsed.fold
                                                            : parsed.checkStats;
            flag = true;
        }
        else
        {
            return invalid(std::format("Unknown option '{}'", name));
        }
    }
    return parsed;
}
}
