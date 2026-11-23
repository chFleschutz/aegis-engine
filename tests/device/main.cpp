import std;
import aegis.test;

namespace aegis::test
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
        else if (arg == "--scene-file" && i + 1 < args.size())
        {
            options.scene = true;
            options.sceneFile = args[++i];
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
        else if (arg == "--view")
        {
            options.scene = true;
            options.view = true;
        }
        else if (arg == "--views")
        {
            options.scene = true;
            options.views = true;
        }
        else if (arg == "--mirror")
        {
            options.scene = true;
            options.mirror = true;
        }
        else if (arg == "--unlit")
        {
            options.scene = true;
            options.unlit = true;
        }
        else if (arg == "--blend")
        {
            options.scene = true;
            options.blend = true;
        }
        else if (arg == "--blend-swap")
        {
            options.scene = true;
            options.blend = true;
            options.blendSwap = true;
        }
        else if (arg == "--double-sided")
        {
            options.scene = true;
            options.doubleSided = true;
        }
        else if (arg == "--worlds")
        {
            options.scene = true;
            options.worlds = true;
        }
        else if (arg == "--post-process")
        {
            options.scene = true;
            options.postProcess = true;
        }
        else if (arg == "--audio")
        {
            options.scene = true;
            options.audio = true;
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
        else if (arg == "--no-samples")
        {
            options.noSamples = true;
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

    if (options.audio)
    {
        if (options.sceneFile != testSceneFile)
            return std::unexpected{ "--audio loads a scene of its own, so it can't run with --scene-file" };
        options.sceneFile = audioSceneFile;
    }
    if (!options.compare.empty() && !options.headless)
        return std::unexpected{ "--compare needs --headless" };
    if (options.postProcess && (options.view || options.views))
        return std::unexpected{ "--post-process renders through a view of its own, so it can't run with --view or --views" };
    if (options.view && options.views)
        return std::unexpected{ "--view and --views each pick the views to render, so they can't run together" };
    if (options.views && options.frames != 0 && options.frames < 50)
        return std::unexpected{ "--views needs at least 50 frames to render the views together and alone" };
    if (options.views && options.unlit)
        return std::unexpected{ "--views adds an unlit cube of its own, so it can't run with --unlit" };
    if (options.doubleSided && (options.unlit || options.views))
        return std::unexpected{ "--double-sided counts the pixels of the unlit colour, as --unlit and --views do, so it can't run with either" };
    if (options.sceneFile != testSceneFile && (options.mirror || options.mutateScene || options.worlds || options.unlit || options.blend || options.doubleSided || options.view || options.views))
        return std::unexpected{ "--mirror, --mutate-scene, --worlds, --unlit, --blend, --blend-swap, --double-sided, --view and --views are written for the test scene, so they can't run with --scene-file" };
    if (options.scene && options.noSamples)
        return std::unexpected{ "--scene loads the sample project, so it can't run with --no-samples" };

    // Headless has no window to close, so it always stops by itself.
    if (options.headless && options.frames == 0)
        options.frames = 600;
    return options;
}
}

auto main(int argc, char** argv) -> int
{
    const auto options = aegis::test::parseOptions(std::span{ argv, static_cast<std::size_t>(argc) });
    if (!options)
    {
        std::println("{}\nUsage: aegis-test [--headless] [--frames <n>] [--scene] [--scene-file <path>] [--gpu-driven] [--mutate-scene] [--mirror] [--unlit] [--blend] [--blend-swap] [--double-sided] [--view] [--views] [--worlds] [--post-process] [--audio] [--no-overlay] [--no-samples] [--screenshot <png>] [--compare <png> [--tolerance <0-255>]]", options.error());
        return 2;
    }

    if (options->scene)
        return aegis::test::runScene(*options);

    auto harness = aegis::test::Harness::create(*options);
    if (!harness)
    {
        std::println("Failed to create the harness \n{}", harness.error());
        return 1;
    }
    return harness->run();
}