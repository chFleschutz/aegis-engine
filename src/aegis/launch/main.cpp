// The engine's launcher: the only main(), compiled into each game's executables by aegis_add_game. The
// runtime flavor (<Game>) runs the game in the engine's GameShell; the editor flavor (<Game>-Editor,
// AEGIS_LAUNCH_EDITOR) installs the editor's shell instead, which edits the game's scenes. The options the engine doesn't
// know are the shell's (the editor's --editor-*, none for the runtime): a bad one exits with the usage code before the
// engine starts, and --help lists them with the engine's. After the game's
// onStartup it loads the scene file named by --scene, else the game's startup scene. AEGIS_GAME_PROJECT_DIR is the game's project folder at configure time, defined
// for development builds only.

import std;
import aegis.engine;
#ifdef AEGIS_LAUNCH_EDITOR
import aegis.editor;
#endif

namespace aegis::launch
{
/// @brief Defined by the game's AEGIS_GAME_MODULE.
auto gameModule() -> GameModule;
}

namespace
{
using namespace aegis;

auto configuredProject() -> std::filesystem::path
{
#ifdef AEGIS_GAME_PROJECT_DIR
    return std::filesystem::path{ std::u8string_view{ u8"" AEGIS_GAME_PROJECT_DIR } };
#else
    return {};
#endif
}

auto isDirectory(const std::filesystem::path& path) -> bool
{
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

// The shell's options for the help text.
constexpr std::span<const OptionHelp> shellHelp{
#ifdef AEGIS_LAUNCH_EDITOR
    editor::optionsHelp
#endif
};

// What --help says the executable is.
auto overview(std::string_view game) -> std::string
{
#ifdef AEGIS_LAUNCH_EDITOR
    return std::format("The editor of {}: opens, edits and saves the scene files of its project.", game);
#else
    return std::format("Runs {}.", game);
#endif
}

// The shell's options, checked before the engine starts so a bad one fails at once; the shell reads them again in
// onStartup.
auto checkShellOptions(std::span<const ShellOption> options) -> std::expected<void, EngineError>
{
#ifdef AEGIS_LAUNCH_EDITOR
    return editor::parseOptions(options).transform([](const editor::EditorOptions&) {});
#else
    return rejectShellOptions(options);
#endif
}

auto run(int argc, char** argv) -> int
{
    const auto game = launch::gameModule();
    // The executable's file name, as it is typed
    const auto program = argc > 0 ? std::filesystem::path{ argv[0] }.filename().string() : std::string{ game.name };
    auto desc = parseArgs(argc, argv);
    if (desc && desc->help)
    {
        std::print("{}", helpText(program, overview(game.name), shellHelp));
        return exitSuccess;
    }
    if (auto checked = desc.and_then([](const AppDesc& parsed) { return checkShellOptions(parsed.shellOptions); }); !checked)
    {
        log::error("{}. {}", describe(checked.error()), tryHelp(program));
        return exitUsage;
    }

    // --project, then `project/` next to the executable (a copied or installed game), then the source tree.
    const auto beside = paths::executableDirectory();
    auto project = paths::locate({
                                     .given = desc->project,
                                     .besideExecutable = beside.empty() ? beside : beside / "project",
                                     .configured = configuredProject(),
                                 },
        isDirectory);
    if (!project)
    {
        log::fatal("No project folder for {}: {}", game.name, project.error());
        return exitFailure;
    }
    desc->project = std::move(*project);
    if (!desc->dumpAsset.empty())
    {
        auto text = dumpAsset(*desc);
        if (!text)
        {
            log::error("{}", describe(text.error()));
            return exitFailure;
        }
        std::print("{}", *text);
        return exitSuccess;
    }
    desc->title = std::string{ game.name };
    log::info("Project '{}', shaders '{}', engine content '{}'", desc->project.string(), paths::shaders().string(),
        paths::content().string());

    auto created = Engine::create(*desc);
    if (!created)
    {
        log::fatal("{}", describe(created.error()));
        return exitFailure;
    }
    auto& engine = **created;

    if (game.registerTypes)
    {
        if (auto registered = game.registerTypes(engine.types()); !registered)
        {
            log::fatal("Failed to register the types of {}: {}", game.name, reflect::describe(registered.error()));
            return exitFailure;
        }
    }
    if (game.setupWorld)
        engine.setWorldSetup([&engine, setup = game.setupWorld](world::World& world) { setup(engine, world); });
    if (game.buildGraph)
        engine.setGraphBuild(game.buildGraph);
#ifdef AEGIS_LAUNCH_EDITOR
    engine.setShell(std::make_unique<editor::EditorShell>());
#endif
    if (game.onStartup)
    {
        if (auto started = game.onStartup(engine); !started)
        {
            log::fatal("{} failed to start: {}", game.name, describe(started.error()));
            return exitFailure;
        }
    }
    if (const std::string_view scene = desc->scene.empty() ? game.startupScene : desc->scene; !scene.empty())
    {
        const auto id = engine.assets().find(scene);
        if (!id)
        {
            log::fatal("No scene file '{}' in the project of {}", scene, game.name);
            return exitFailure;
        }
        if (auto loaded = engine.loadScene(*id); !loaded)
        {
            log::fatal("{}", describe(loaded.error()));
            return exitFailure;
        }
    }
    return engine.run();
}
}

auto main(int argc, char** argv) -> int
{
    return run(argc, argv);
}
