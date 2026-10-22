// The engine's launcher: the only main(), compiled into each game's executables by aegis_add_game. The
// runtime flavor (<Game>) runs the game in the engine's GameShell; the editor flavor (<Game>-Editor,
// AEGIS_LAUNCH_EDITOR) installs the editor's shell instead, which edits the game's scenes. After the game's
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

auto run(int argc, char** argv) -> int
{
    const auto game = launch::gameModule();
    auto desc = parseArgs(argc, argv);
    if (!desc)
    {
        log::error("{}\n{}", describe(desc.error()), usage(argc > 0 ? std::string_view{ argv[0] } : game.name));
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
#ifdef AEGIS_LAUNCH_EDITOR
    engine.setShell(std::make_unique<editor::EditorShell>()); // Before the startup scene, which begins in edit mode
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
