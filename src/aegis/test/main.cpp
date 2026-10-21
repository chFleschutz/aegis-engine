import std;
import aegis.test;

namespace aegis
{
auto parseOptions(std::span<char*> args) -> std::expected<Options, std::string>
{
    Options options;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const std::string_view arg{ args[i] };
        if (arg == "--headless")
        {
            options.headless = true;
        }
        else if (arg == "--scene")
        {
            options.scene = true;
        }
        else if (arg == "--gpu-driven")
        {
            options.scene = true;
            options.gpuDriven = true;
        }
        else if (arg == "--mutate-scene")
        {
            options.scene = true;
            options.mutateScene = true;
        }
        else if (arg == "--screenshot" && i + 1 < args.size())
        {
            options.screenshot = args[++i];
        }
        else if (arg == "--compare" && i + 1 < args.size())
        {
            options.compare = args[++i];
        }
        else if (arg == "--tolerance" && i + 1 < args.size())
        {
            const std::string_view value{ args[++i] };
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.tolerance);
            if (error != std::errc{} || end != value.data() + value.size() || options.tolerance < 0 || options.tolerance > 255)
                return std::unexpected{ std::format("--tolerance needs an integer from 0 to 255, got '{}'", value) };
        }
        else if (arg == "--no-overlay")
        {
            options.noOverlay = true;
        }
        else if (arg == "--frames" && i + 1 < args.size())
        {
            const std::string_view value{ args[++i] };
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.frames);
            if (error != std::errc{} || end != value.data() + value.size() || options.frames == 0)
                return std::unexpected{ std::format("--frames needs a positive integer, got '{}'", value) };
        }
        else
        {
            return std::unexpected{ std::format("Unknown argument '{}'", arg) };
        }
    }

    if (!options.compare.empty() && !options.headless)
        return std::unexpected{ "--compare needs --headless" };

    // Headless has no window to close, so it always stops by itself.
    if (options.headless && options.frames == 0)
        options.frames = 600;
    return options;
}
}

auto main(int argc, char** argv) -> int
{
    const auto options = aegis::parseOptions(std::span{ argv, static_cast<std::size_t>(argc) });
    if (!options)
    {
        std::println("{}\nUsage: aegis-test [--headless] [--frames <n>] [--scene] [--gpu-driven] [--mutate-scene] [--no-overlay] [--screenshot <png>] [--compare <png> [--tolerance <0-255>]]", options.error());
        return 2;
    }

    auto engine = aegis::Engine::create(*options);
    if (!engine)
    {
        if (engine.error() == skipMeshShaders)
        {
            std::println("{}", engine.error());
            return skipExitCode;
        }
        std::println("Failed to create engine \n{}", engine.error());
        return 1;
    }
    return engine->run();
}