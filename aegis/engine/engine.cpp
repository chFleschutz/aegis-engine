module;

#include <cassert>
#include <cstdlib>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

module aegis.engine;

import std;
import aegis.paths;
import aegis.physics;
import aegis.project_file;
import aegis.render_passes;
import aegis.scripting;

namespace aegis
{
namespace
{
// What ScriptContext::spawn instantiates through: the engine's asset library and types.
class AssetSpawner final : public scripting::Spawner
{
public:
    AssetSpawner(AssetLibrary& assets, const reflect::TypeRegistry& types) :
        m_assets(assets),
        m_types(types)
    {
    }

    auto spawn(world::World& world, assets::AssetId scene, world::Entity parent, std::uint64_t holderId)
        -> std::expected<world::Entity, std::string> override
    {
        auto holder = m_assets.instantiateScene(world, m_types, scene, parent, holderId);
        if (!holder)
            return std::unexpected{ describe(holder.error()) };
        return *holder;
    }

private:
    AssetLibrary& m_assets;
    const reflect::TypeRegistry& m_types;
};

constexpr std::uint32_t DEFAULT_HEADLESS_FRAMES{ 600 };

// parseArgs's options, for helpText
constexpr std::array ENGINE_OPTIONS{
    OptionHelp{ "--help", "Print this help and exit." },
    OptionHelp{ "--headless", "Render offscreen, with no window, input or UI. The run stops by itself,\nafter 600 frames unless --frames says otherwise." },
    OptionHelp{ "--frames <n>", "Render <n> frames, then exit." },
    OptionHelp{ "--screenshot <png>", "Save the last frame as PNG. Needs --headless." },
    OptionHelp{ "--width <n>", "The window's width, or the offscreen target's when headless (1920)." },
    OptionHelp{ "--height <n>", "The window's height, or the offscreen target's when headless (1080)." },
    OptionHelp{ "--project <dir>", "The project folder the assets come from. Defaults to project/ next to\nthe executable, then the game's folder in the source tree." },
    OptionHelp{ "--scene <path>", "The scene file to start with, by its path in the project's content/\nfolder. Defaults to the game's own." },
    OptionHelp{ "--dump-asset <id>", "Print the import cache entry of the asset with this ID (16 hex digits)\nand exit." },
};
// Fixed, so a headless run does not depend on the machine; one fixed step of the worlds per frame
constexpr float HEADLESS_FRAME_DELTA_SEC{ world::FixedStep::DEFAULT_STEP_SEC };

auto parseCount(std::string_view option, std::string_view value) -> std::expected<std::uint32_t, EngineError>
{
    std::uint32_t count{ 0 };
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), count);
    if (error != std::errc{} || end != value.data() + value.size() || count == 0)
        return std::unexpected{ makeError(EngineErrorCode::InvalidArguments, std::format("{} needs a positive integer, got '{}'", option, value)) };
    return count;
}

// The asset library's folders: the standard ones, or the game's project with its `.aegis` folder (the import cache
// in AEGIS_CACHE_DIR if it is set, so checkouts can share one).
auto assetSources(const AppDesc& desc) -> AssetLibrary::Sources
{
    auto sources = AssetLibrary::Sources::standard();
    if (!desc.project.empty())
    {
        sources.project = desc.project;
        sources.indexFile = desc.project / ".aegis" / "index";
        sources.importCache = paths::cacheOverride().value_or(desc.project / ".aegis" / "cache") / "imported";
    }
    return sources;
}
}

auto makeError(EngineErrorCode code, std::string message) -> EngineError
{
    return EngineError{ .code = code, .message = std::move(message) };
}

auto describe(const EngineError& error) -> std::string
{
    return error.message;
}

auto parseArgs(std::span<const char* const> args) -> std::expected<AppDesc, EngineError>
{
    AppDesc desc;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const std::string_view arg{ args[i] };
        const auto hasValue = i + 1 < args.size();
        const auto takesValue = arg == "--screenshot" || arg == "--project" || arg == "--scene" || arg == "--dump-asset" ||
                                arg == "--frames" || arg == "--width" || arg == "--height";
        if (takesValue && !hasValue)
            return std::unexpected{ makeError(EngineErrorCode::InvalidArguments, std::format("{} needs a value", arg)) };

        if (arg == "--help")
        {
            desc.help = true;
        }
        else if (arg == "--headless")
        {
            desc.headless = true;
        }
        else if (arg == "--screenshot")
        {
            desc.screenshot = args[++i];
        }
        else if (arg == "--project")
        {
            desc.project = args[++i]; // argv is in the native narrow encoding, as path expects
        }
        else if (arg == "--scene")
        {
            desc.scene = args[++i];
        }
        else if (arg == "--dump-asset")
        {
            desc.dumpAsset = args[++i];
        }
        else if (arg == "--frames" || arg == "--width" || arg == "--height")
        {
            const auto count = parseCount(arg, args[++i]);
            if (!count)
                return std::unexpected{ count.error() };
            auto& target = arg == "--frames" ? desc.frames : arg == "--width" ? desc.width
                                                                              : desc.height;
            target = *count;
        }
        else if (arg.starts_with("--") && arg.size() > 2)
        {
            // The shell's: it validates them in onStartup
            auto& option = desc.shellOptions.emplace_back(ShellOption{ .name = std::string{ arg } });
            if (hasValue && !std::string_view{ args[i + 1] }.starts_with("--"))
                option.value = args[++i];
        }
        else
        {
            return std::unexpected{ makeError(EngineErrorCode::InvalidArguments, std::format("Unknown argument '{}'", arg)) };
        }
    }

    if (!desc.screenshot.empty() && !desc.headless)
        return std::unexpected{ makeError(EngineErrorCode::InvalidArguments, "--screenshot needs --headless") };
    // Headless has no window to close, so it always stops by itself.
    if (desc.headless && desc.frames == 0)
        desc.frames = DEFAULT_HEADLESS_FRAMES;
    return desc;
}

auto parseArgs(int argc, char** argv) -> std::expected<AppDesc, EngineError>
{
    const std::vector<const char*> args(argv, argv + argc);
    return parseArgs(std::span<const char* const>{ args });
}

auto helpText(std::string_view program, std::string_view overview, std::span<const OptionHelp> shellOptions) -> std::string
{
    std::vector<OptionHelp> options{ std::from_range, ENGINE_OPTIONS };
    options.append_range(shellOptions);
    std::ranges::sort(options, {}, &OptionHelp::name);
    const auto width = std::ranges::max(options | std::views::transform([](const OptionHelp& option) { return option.name.size(); }));

    auto text = std::format("OVERVIEW: {}\n\nUSAGE: {} [options]\n\nOPTIONS:\n", overview, program);
    for (const auto& option : options)
    {
        // "  --name   - first line", the description's further lines under its first
        auto lines = option.description | std::views::split('\n');
        auto line = lines.begin();
        std::format_to(std::back_inserter(text), "  {:<{}} - {}\n", option.name, width, std::string_view{ *line });
        for (++line; line != lines.end(); ++line)
            std::format_to(std::back_inserter(text), "  {:<{}}   {}\n", "", width, std::string_view{ *line });
    }
    return text;
}

auto tryHelp(std::string_view program) -> std::string
{
    return std::format("Try '{} --help'.", program);
}

auto rejectShellOptions(std::span<const ShellOption> options) -> std::expected<void, EngineError>
{
    if (options.empty())
        return {};
    return std::unexpected{ makeError(EngineErrorCode::InvalidArguments, std::format("Unknown option '{}'", options.front().name)) };
}

auto GameShell::onStartup(Engine& engine) -> std::expected<void, EngineError>
{
    return rejectShellOptions(engine.desc().shellOptions);
}

auto dumpAsset(const AppDesc& desc) -> std::expected<std::string, EngineError>
{
    const auto id = assets::AssetId::parse(desc.dumpAsset);
    if (!id)
        return std::unexpected{ makeError(EngineErrorCode::InvalidArguments, std::format("--dump-asset needs an asset ID (16 lowercase hex digits), got '{}'", desc.dumpAsset)) };

    const auto sources = assetSources(desc);
    const auto database = assets::AssetDatabase::scan({
        .roots = { { paths::projectContent(sources.project) }, { sources.engineContent, true } },
        .indexFile = sources.indexFile,
    });
    if (!database)
        return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, std::format("Failed to scan the assets: {}", assets::describe(database.error()))) };
    const auto* entry = database->find(*id);
    if (!entry)
        return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, std::format("No asset has the ID {}", *id)) };

    const assets::ImportCache cache{ sources.importCache };
    const auto importer = assets::importerFor(entry->path);
    if (entry->key.empty() && (importer == "texture" || importer == "environment"))
    {
        // A texture entry is found by the image's bytes and settings.
        std::ifstream file{ database->absolutePath(*entry), std::ios::binary };
        if (!file)
            return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, std::format("Cannot read '{}'", entry->path)) };
        const std::string bytes{ std::istreambuf_iterator<char>{ file }, {} };
        auto text = cache.dumpTexture(std::as_bytes(std::span{ bytes }),
            { .name = entry->path, .srgb = assets::textureSrgb(entry->settings, true), .environment = importer == "environment" });
        if (!text)
            return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, assets::describe(text.error())) };
        return std::move(*text);
    }

    // A mesh or an embedded texture is a part of its model file's entry; a texture's settings are in the model's sidecar.
    auto model = *id;
    std::string_view partKey;
    assets::ImportSettings partSettings;
    if (!entry->key.empty())
    {
        const auto owner = database->find(entry->root, entry->path);
        if ((entry->type != "StaticMesh" && entry->type != "Texture") || !owner)
            return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, std::format("{} is '{}' of '{}'; only models, their meshes and textures are cached", *id, entry->key, entry->path)) };
        model = *owner;
        partKey = entry->key;
        std::ifstream file{ assets::sidecarPath(database->absolutePath(*entry)) };
        if (const auto sidecar = assets::parseSidecar(std::string{ std::istreambuf_iterator<char>{ file }, {} }))
        {
            if (const auto found = sidecar->partSettings.find(entry->key); found != sidecar->partSettings.end())
                partSettings = found->second;
        }
    }
    else if (importer != "model")
    {
        return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, std::format("{} is '{}'; only models, their meshes and textures are cached", *id, entry->path)) };
    }

    const auto* modelEntry = database->find(model);
    auto text = cache.dump(database->absolutePath(*modelEntry), modelEntry->settings, partKey, partSettings);
    if (!text)
        return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, assets::describe(text.error())) };
    return std::move(*text);
}

auto createEngine(int argc, char** argv) -> std::expected<std::unique_ptr<Engine>, int>
{
    const auto desc = parseArgs(argc, argv);
    if (!desc)
    {
        log::error("{}. {}", describe(desc.error()), tryHelp(argc > 0 ? argv[0] : "app"));
        return std::unexpected{ exitUsage };
    }
    auto engine = Engine::create(*desc);
    if (!engine)
    {
        log::fatal("{}", describe(engine.error()));
        return std::unexpected{ exitFailure };
    }
    return std::move(*engine);
}

void FrameContext::update(world::World& world, world::UpdateMode mode)
{
    assert(engine.m_worlds.contains(world) && "FrameContext::update: not one of the engine's worlds");
    auto& updates = engine.m_updates;
    if (const auto found = std::ranges::find(updates, &world, &Engine::WorldUpdate::world); found != updates.end())
        found->mode = mode;
    else
        updates.push_back({ .world = &world, .mode = mode });
}

void FrameContext::render(world::World& world)
{
    assert(engine.m_worlds.contains(world) && "FrameContext::render: not one of the engine's worlds");
    engine.m_renderRequest = &world;
}

auto FrameContext::instances(const world::World& world) const -> const render_graph::InstanceStore&
{
    assert(engine.m_worlds.contains(world) && "FrameContext::instances: not one of the engine's worlds");
    return engine.m_worlds.instances(world);
}

auto Engine::loadScene(assets::AssetId scene) -> std::expected<void, EngineError>
{
    newScene();
    if (auto created = m_assets->instantiateScene(world(), m_types, scene); !created)
        return std::unexpected{ makeError(EngineErrorCode::SceneFailed, describe(created.error())) };
    return {};
}

void Engine::newScene()
{
    resetWorld();
}

void Engine::setTitle(std::string_view title)
{
    if (m_platform->window)
        m_platform->window->setTitle(title);
}

void Engine::restoreCursor()
{
    if (m_platform->input)
        m_platform->input->setCursorMode(platform::CursorMode::Normal);
}

auto Engine::contentScale() const -> float
{
    return m_platform->window ? m_platform->window->contentScale() : 1.0f;
}

auto Engine::createWorld() -> world::World&
{
    auto& created = m_worlds.create();
    // Headless, a fixed step, so a run does not depend on the machine.
    created.clock() = m_desc.headless ? core::FrameClock::fixed(HEADLESS_FRAME_DELTA_SEC) : core::FrameClock::clamped(MAX_FRAME_DELTA_SEC);
    return created;
}

void Engine::destroyWorld(world::World& world)
{
    assert(&world != m_mainWorld && "Engine::destroyWorld: the main world is never destroyed");
    std::erase_if(m_updates, [&world](const WorldUpdate& update) { return update.world == &world; });
    if (m_renderRequest == &world)
        m_renderRequest = nullptr;
    if (m_lastRendered == &world)
        m_lastRendered = nullptr; // A world created at its address later has other instances
    m_worlds.destroy(world);
}

void Engine::setWorldSetup(std::function<void(world::World& world)> setup)
{
    m_worldSetup = std::move(setup);
    if (!m_worldSetup)
        return;
    for (auto* existing : m_worlds.all())
        m_worldSetup(*existing);
}

void Engine::setUpWorld(world::World& world)
{
    // The engine's systems, always present. Their phases order them (scripts, physics, then
    // transforms, then cameras); the game's come after them (setWorldSetup), in the phase they declare.
    if (m_platform->input)
        world.addSystem<scripting::ScriptSystem>(m_platform->input->state(), m_platform->input.get());
    else
        world.addSystem<scripting::ScriptSystem>(m_platform->headlessInput); // Scripts see the input map, and what checks apply
    world.addSystem<physics::PhysicsSystem>();                               // After the scripts, whose fixed steps move kinematic bodies
    world.addSystem<world::TransformSystem>();
    world.addSystem<world::CameraSystem>();
    if (m_spawner)
        world.registry().setContext<scripting::Spawner>(*m_spawner);
    world.addSystem<audio::AudioSystem>(*m_platform->audio);
    if (m_worldSetup)
        m_worldSetup(world);
}

void Engine::resetWorld()
{
    m_worlds.reset(world());
}

auto graphViews(std::span<const ViewTarget> targets) -> std::vector<render_graph::DefaultPassesDesc::View>
{
    std::vector<render_graph::DefaultPassesDesc::View> views;
    for (const auto& target : targets)
        views.push_back({ .name = target.name, .extent = target.extent });
    if (views.empty())
        views.push_back({ .name = std::string{ render_graph::names::mainView } });
    // Each main view samples a shadow view of the directional light.
    render_graph::addShadowViews(views);
    return views;
}

auto buildFrameGraph(render_graph::GraphBuilder& graph, const render_graph::DefaultPassesDesc& desc, const GraphBuild& game,
    Shell& shell) -> void
{
    auto viewDesc = desc;
    viewDesc.lights = render_graph::addSharedPasses(graph, desc).lights;
    for (const auto index : render_graph::viewDeclarationOrder(desc.views))
    {
        const auto view = render_graph::ViewContext::of(desc.views, index);
        graph.beginScope(view.name, view.extent);
        if (desc.views[index].kind == render_graph::ViewKind::Shadow)
            render_graph::addShadowPasses(graph, viewDesc, view);
        else
        {
            if (game)
                game(graph, viewDesc, view);
            else
                render_graph::addScenePasses(graph, viewDesc, view);
            shell.buildGraph(graph, viewDesc, view);
        }
        graph.endScope();
    }
    render_graph::addOutputPasses(graph, viewDesc);
}

void Engine::setShell(std::unique_ptr<Shell> shell)
{
    m_shell = shell ? std::move(shell) : std::make_unique<GameShell>();
}

Engine::Engine(const AppDesc& desc) :
    m_desc{ desc }
{
}

auto Engine::create(const AppDesc& desc) -> std::expected<std::unique_ptr<Engine>, EngineError>
{
    std::unique_ptr<Engine> engine{ new Engine{ desc } };
    if (auto initialized = engine->initialize(); !initialized)
        return std::unexpected{ std::move(initialized.error()) };
    return engine;
}

auto Engine::initialize() -> std::expected<void, EngineError>
{
    auto registered = world::registerComponents(m_types)
                          .and_then([this] { return serialize::registerComponents(m_types); })
                          .and_then([this] { return scripting::registerKinematicMovementController(m_types); })
                          .and_then([this] { return audio::registerComponents(m_types); })
                          .and_then([this] { return physics::registerComponents(m_types); });
    if (!registered)
        return std::unexpected{ makeError(EngineErrorCode::TypesFailed, std::format("Failed to register the built-in types: {}", reflect::describe(registered.error()))) };
    auto platform = Platform::create(m_desc);
    if (!platform)
        return std::unexpected{ std::move(platform.error()) };
    m_platform = std::move(*platform);
    if (m_platform->window)
        m_platform->window->setCloseHandler([this] { return m_shell->canClose(); });
    if (auto loaded = loadProjectFile(); !loaded)
        return loaded;
    auto gpu = Gpu::create(m_desc, m_platform->window.get());
    if (!gpu)
        return std::unexpected{ std::move(gpu.error()) };
    m_gpu = std::move(*gpu);
    if (auto created = createAssets(); !created)
        return created;
    m_mainWorld = &createWorld();
    auto presentation = Presentation::create({
        .window = m_platform->window.get(),
        .context = *m_gpu->context,
        .device = *m_gpu->device,
        .renderer = *m_gpu->renderer,
        .framesInFlight = FRAMES_IN_FLIGHT,
        .gpuDriven = useGpuDrivenRendering(),
        // The game's passes and the shell as they are when the graph is built (setGraphBuild, setShell)
        .declare = [this](render_graph::GraphBuilder& graph, const render_graph::DefaultPassesDesc& desc) { buildFrameGraph(graph, desc, m_graphBuild, *m_shell); },
    });
    if (!presentation)
        return std::unexpected{ std::move(presentation.error()) };
    m_presentation = std::move(*presentation);

    log::info("Engine Initialized!");
    log::info("Using {} Driven Rendering{}", useGpuDrivenRendering() ? "GPU" : "CPU", m_desc.headless ? " (headless)" : "");
    log::info("\n\n\n"
              "\t\t\t\t      ###   #######   ######   ####   ###### \n"
              "\t\t\t\t     ## ##  ##       ##    ##   ##   ##    ##\n"
              "\t\t\t\t    ##  ##  ##       ##         ##   ##      \n"
              "\t\t\t\t   ##   ##  ######   ##         ##    ###### \n"
              "\t\t\t\t  ########  ##       ##  ####   ##         ##\n"
              "\t\t\t\t ##     ##  ##       ##    ##   ##   ##    ##\n"
              "\t\t\t\t##      ##  #######   ######   ####   ###### \n"
              "\n\n");
    return {};
}

Engine::~Engine()
{
    // The GPU may still use what the members free. The shell ends first, while the worlds, the frame graph and ImGui
    // still exist; the members then go bottom up. A failed create() leaves the later members empty.
    if (m_gpu)
        m_gpu->device->waitIdle();
    m_shell->onShutdown();
    m_shell.reset();
}

auto Engine::useGpuDrivenRendering() const -> bool
{
    return m_desc.gpuDriven && m_gpu->device->capabilities().meshShaders;
}

auto Engine::run() -> int
{
    const bool headless = this->headless();
    auto* window = m_platform->window.get();
    auto& device = *m_gpu->device;
    auto& renderer = *m_gpu->renderer;
    std::uint32_t frames{ 0 };
    int exitCode{ exitSuccess };

    if (auto started = m_shell->onStartup(*this); !started)
    {
        // A bad shell option is a bad command line
        if (started.error().code == EngineErrorCode::InvalidArguments)
        {
            log::error("{}", describe(started.error()));
            return exitUsage;
        }
        log::fatal("The shell failed to start: {}", describe(started.error()));
        return exitFailure;
    }
    // Once the game's passes (setGraphBuild) and the shell, with its options, are known.
    if (auto created = m_presentation->buildGraph(); !created)
    {
        log::fatal("{}", describe(created.error()));
        return exitFailure;
    }

    if (headless)
        m_clock = core::FrameClock::fixed(HEADLESS_FRAME_DELTA_SEC);
    resetClocks(std::chrono::steady_clock::now());
    while (!m_quit && (headless || !window->shouldClose()) && (m_desc.frames == 0 || frames < m_desc.frames))
    {
        ScopeProfiler frameTime("Frame Time");

        const auto currentFrameBegin = std::chrono::steady_clock::now();

        if (!headless)
        {
            window->update();
            if (window->isMinimized())
            {
                window->waitEvents();
                resetClocks(std::chrono::steady_clock::now()); // Not part of the next frame's delta
                continue;
            }
            if (window->wasResized() || renderer.needsResize())
            {
                if (!resize())
                {
                    exitCode = exitFailure;
                    break;
                }
            }

            // The shell decides what the game doesn't see, from the UI it built last frame.
            captureInput();
            m_platform->input->update();
        }
        // The clock clamps a long frame (a breakpoint, a load) so the UI doesn't get the whole stall; so do the worlds'.
        const float frameTimeSec = m_clock.tick(currentFrameBegin);
        // The graph follows the views the shell's UI asked for last frame, before this frame's UI shows their textures.
        if (auto followed = m_presentation->followViews(m_shell->renderViews()); !followed)
        {
            log::fatal("{}", describe(followed.error()));
            exitCode = exitFailure;
            break;
        }
        // The safe point: the shell's UI and edits come before the update, so an edit shows this frame. The shell
        // also picks the worlds that update and the one that renders.
        runShell(frameTimeSec);
        // A shortcut the shell ran this frame keeps its key from the game's update too.
        if (!headless)
            captureInput();
        // The cameras the shell's UI asks for this frame; every world's main camera gets the aspect it renders at.
        const auto views = m_shell->renderViews();
        updateCameraAspect(views);
        // Update: each world's systems run phase by phase, scripts first: they write the transforms, the others
        // derive from them (camera matrices included). In edit mode only the derived ones run.
        updateWorlds(currentFrameBegin);
        // Headless, the sounds advance by the frame's time; either way the finished ones are freed.
        m_platform->audio->advance(frameTimeSec);

        // Rendering: uploads queued this frame (or while loading) go first, on the same queue.
        if (auto flushed = device.flushUploads(); !flushed)
        {
            log::fatal("Failed to flush uploads: {}", rhi::describe(flushed.error()));
            exitCode = exitFailure;
            break;
        }
        // The passes draw one world's instances, camera, lights and sky as its last update left them.
        auto& shown = m_renderRequest ? *m_renderRequest : world();
        auto& instances = m_worlds.instances(shown);
        // Another world than last frame: the passes' buffers hold the other one's instances, so all of these are written.
        if (&shown != m_lastRendered)
        {
            instances.markAllDirty();
            m_lastRendered = &shown;
        }
        m_worlds.sync(shown);
        const auto scene = frameScene(shown, instances, views);
        // The passes read it until the end of this frame's iteration.
        const auto boundScene = m_presentation->frameScene().bind(scene);
        // What recording can't do without (the draw batches' pipelines, buffers grown to the frame's instances and
        // lights) is created before it; failing that ends the run, as a failed frame does.
        auto& frameGraph = m_presentation->frameGraph();
        if (auto prepared = frameGraph.prepare(); !prepared)
        {
            log::fatal("Failed to prepare a frame: {}", render_graph::describe(prepared.error()));
            exitCode = exitFailure;
            break;
        }
        auto rendered = renderer.renderFrame([this, &frameGraph, &instances](const aegis::renderer::Renderer::FrameInfo& frame) {
            m_lastFrameIndex = frame.frameIndex;
            frameGraph.execute(frame.cmd, frame.frameIndex, frame.target);
            // Recorded: the next frame needs only what changes after this one.
            instances.endFrame();
        });
        if (!rendered)
        {
            log::fatal("Failed to render a frame: {}", aegis::renderer::describe(rendered.error()));
            exitCode = exitFailure;
            break;
        }
        // Frame Time less this is the CPU's time (the editor's status bar)
        Profiler::instance().addTime("Frame Wait", renderer.waitMs());
        ++frames;
    }

    device.waitIdle();

    if (exitCode == exitSuccess && !m_desc.screenshot.empty())
    {
        if (auto saved = saveScreenshot(); !saved)
        {
            log::error("Failed to save the screenshot: {}", saved.error());
            exitCode = exitFailure;
        }
    }
    // Validation errors never throw; like aegis-test, a run that caused one has failed.
    if (const auto errors = rhi::Context::validationErrorCount(); errors > 0)
    {
        log::error("Validation failed: {} errors reported (see stderr)", errors);
        exitCode = exitFailure;
    }
    return exitCode;
}

auto Engine::Platform::create(const AppDesc& desc) -> std::expected<std::unique_ptr<Platform>, EngineError>
{
    auto created = std::make_unique<Platform>();
    if (!desc.headless)
    {
        auto window = platform::Window::create({ .title = desc.title, .width = desc.width, .height = desc.height });
        if (!window)
            return std::unexpected{ makeError(EngineErrorCode::WindowFailed, std::format("Failed to create the window: {}", platform::describe(window.error()))) };
        created->window = std::make_unique<platform::Window>(std::move(*window));
        created->input = std::make_unique<platform::Input>(*created->window);
    }
    // Headless runs never open a device, so they behave the same on every machine.
    auto audio = audio::AudioEngine::create({ .device = !desc.headless });
    if (!audio)
        return std::unexpected{ makeError(EngineErrorCode::AudioFailed, std::format("Failed to start the audio: {}", audio::describe(audio.error()))) };
    created->audio = std::move(*audio);
    return created;
}

auto Engine::loadProjectFile() -> std::expected<void, EngineError>
{
    auto file = aegis::loadProjectFile(m_desc.project.empty() ? paths::project() : m_desc.project);
    if (!file)
        return std::unexpected{ makeError(EngineErrorCode::ProjectFailed, std::format("Failed to read the project file {}", file.error())) };
    if (m_platform->input)
        m_platform->input->setMap(std::move(file->input));
    else
        m_platform->headlessInput.setMap(std::move(file->input));
    return {};
}

auto Engine::Gpu::create(const AppDesc& desc, platform::Window* window) -> std::expected<std::unique_ptr<Gpu>, EngineError>
{
    auto created = std::make_unique<Gpu>();
    auto context = rhi::Context::create({ .appName = desc.title.c_str(), .window = window });
    if (!context)
        return std::unexpected{ makeError(EngineErrorCode::DeviceFailed, std::format("Failed to create the Vulkan context ({})", rhi::describe(context.error()))) };
    created->context = std::move(*context);

    auto device = created->context->createDevice({});
    if (!device)
        return std::unexpected{ makeError(EngineErrorCode::DeviceFailed, std::format("Failed to create the Vulkan device ({})", rhi::describe(device.error()))) };
    created->device = std::move(*device);

    auto renderer = renderer::Renderer::create({
        .context = *created->context,
        .device = *created->device,
        .window = window,
        .offscreen = { .extent = { desc.width, desc.height } },
    });
    if (!renderer)
        return std::unexpected{ makeError(EngineErrorCode::RendererFailed, std::format("Failed to create the renderer: {}", renderer::describe(renderer.error()))) };
    created->renderer = std::move(*renderer);
    return created;
}

auto Engine::createAssets() -> std::expected<void, EngineError>
{
    auto assets = AssetLibrary::create(*m_gpu->device, FRAMES_IN_FLIGHT, assetSources(m_desc));
    if (!assets)
        return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, describe(assets.error())) };
    m_assets.emplace(std::move(*assets));
    m_spawner = std::make_unique<AssetSpawner>(*m_assets, m_types);
    return {};
}

bool Engine::resize()
{
    auto& renderer = *m_gpu->renderer;
    if (auto resized = renderer.resize(rhi::Extent2D{ m_platform->window->extent() }); !resized)
    {
        log::fatal("Failed to resize the renderer: {}", renderer::describe(resized.error()));
        return false;
    }
    m_platform->window->resetResized();
    if (auto followed = m_presentation->targetResized(); !followed)
        log::fatal("{}", describe(followed.error()));
    return true;
}

void Engine::captureInput()
{
    const auto capture = m_shell->inputCapture();
    m_platform->input->setCaptured(capture.keyboard, capture.mouse);
}

void Engine::runShell(float deltaSeconds)
{
    m_updates.clear();
    m_renderRequest = nullptr;
    m_presentation->beginFrame(m_gpu->renderer->extent(), deltaSeconds);
    FrameContext frame{
        .engine = *this,
        .world = world(),
        .types = m_types,
        .assets = *m_assets,
        .frameGraph = m_presentation->frameGraph(),
        .views = m_presentation->views(),
        .presentation = *m_presentation,
        .renderer = *m_gpu->renderer,
        .deltaSeconds = deltaSeconds,
    };
    m_shell->onFrame(frame);
    m_presentation->endFrame();
}

void Engine::updateWorlds(core::FrameClock::Clock::time_point now)
{
    for (auto* each : m_worlds.all())
    {
        const auto update = std::ranges::find(m_updates, each, &WorldUpdate::world);
        if (update == m_updates.end())
            each->clock().reset(now); // It stands still: the time until it updates again is no step
        else
            m_worlds.update(*each, each->clock().tick(now), update->mode);
    }
}

void Engine::resetClocks(core::FrameClock::Clock::time_point now)
{
    m_clock.reset(now);
    for (auto* each : m_worlds.all())
        each->clock().reset(now);
}

void Engine::updateCameraAspect(const std::vector<RenderView>& views)
{
    // The main camera renders into the first view without a camera of its own, or onto the window.
    const auto& targets = m_presentation->views();
    auto extent = m_platform->window ? rhi::Extent2D{ m_platform->window->extent() } : m_gpu->renderer->extent();
    if (!targets.empty())
    {
        const auto throughMain = std::ranges::find_if(targets, [&](const ViewTarget& target) {
            const auto view = std::ranges::find_if(views, [&](const RenderView& each) {
                return (each.name.empty() ? render_graph::names::mainView : std::string_view{ each.name }) == target.name;
            });
            return view == views.end() || !view->camera;
        });
        extent = throughMain != targets.end() ? throughMain->extent : targets.front().extent;
    }
    if (extent.y == 0)
        return;
    const auto aspect = static_cast<float>(extent.x) / static_cast<float>(extent.y);
    for (auto* each : m_worlds.all())
    {
        auto& registry = each->registry();
        if (const auto mainCamera = each->mainCamera(); mainCamera && registry.has<CameraMatrices>(mainCamera))
            registry.get<CameraMatrices>(mainCamera).aspect = aspect;
    }
}

auto Engine::frameScene(world::World& shown, const render_graph::InstanceStore& instances, const std::vector<RenderView>& views)
    -> render_graph::FrameScene
{
    auto scene = aegis::extractFrameScene(shown, instances);
    // The views the graph renders, from the list it was built from (graphViews), in its order. A main view renders
    // through the camera of the shell's view of its name asked for this frame, else the world's main camera; without
    // views of its own (the graph follows the shell's from the next frame) always the main camera, onto the target.
    const auto mainCamera = scene.camera(0);
    const auto& targets = m_presentation->views();
    scene.views = render_graph::frameSceneViews(graphViews(targets));
    const CameraSource mainSource{ .world = &shown, .mainCamera = shown.mainCamera() };
    std::unordered_map<std::string, CameraSource> sources;
    for (auto& target : scene.views)
    {
        if (target.kind != render_graph::ViewKind::Main)
            continue;
        auto source = mainSource;
        if (targets.empty())
            target.camera = mainCamera;
        else
        {
            const auto view = std::ranges::find_if(views, [&](const RenderView& each) {
                return (each.name.empty() ? render_graph::names::mainView : std::string_view{ each.name }) == target.name;
            });
            const bool throughShell = view != views.end() && view->camera;
            target.camera = throughShell ? view->camera : mainCamera;
            if (throughShell)
                source = CameraSource{};
        }
        if (!target.camera)
            continue;
        const auto last = m_cameraSources.find(target.name);
        if (last == m_cameraSources.end() || last->second != source)
            target.camera->cut = true;
        sources.emplace(target.name, source);
    }
    m_cameraSources = std::move(sources);
    render_graph::fitShadowViews(scene);
    return scene;
}

auto Engine::readBack(const renderer::Texture& image, rhi::Extent2D extent, bool view) -> std::expected<std::vector<std::byte>, std::string>
{
    // A view's target is its "Final" (RGBA16F), left sampled by the UI pass; the offscreen target a color attachment.
    const auto state = view ? rhi::ResourceState::ShaderReadFragment : rhi::ResourceState::ColorAttachment;
    const std::size_t texelSize{ view ? 4 * sizeof(std::uint16_t) : 4 };
    const auto byteSize = std::size_t{ extent.x } * extent.y * texelSize;

    auto readback = m_gpu->device->createBuffer({
        .name = "ScreenshotReadback",
        .size = byteSize,
        .usage = rhi::BufferUsage::TransferDst,
        .memory = rhi::MemoryUsage::CpuRead,
    });
    if (!readback)
        return std::unexpected{ "failed to create the readback buffer" };

    auto pool = m_gpu->device->createCommandPool({ .name = "ScreenshotReadback", .queueFamily = m_gpu->device->graphicsQueue().family() });
    if (!pool)
        return std::unexpected{ "failed to create the command pool" };
    auto cmd = m_gpu->device->createCommandBuffer({ .name = "ScreenshotReadback", .pool = *pool });
    if (!cmd)
        return std::unexpected{ "failed to create the command buffer" };

    // The image goes back to the state it was in, so the next frame's barriers still hold.
    const std::array regions{ rhi::BufferImageCopy{ .extent = rhi::Extent3D{ extent } } };
    cmd->begin(true);
    cmd->transitionImageLayout(image.view, state, rhi::ResourceState::CopySrc);
    cmd->copyImageToBuffer(image.image, m_gpu->device->get(*readback), regions);
    cmd->transitionImageLayout(image.view, rhi::ResourceState::CopySrc, state);
    cmd->end();
    auto submitted = m_gpu->device->graphicsQueue().submit(*cmd);
    if (!submitted)
        return std::unexpected{ "failed to submit the readback" };
    m_gpu->device->graphicsQueue().wait(*submitted);

    std::vector<std::byte> pixels(byteSize);
    m_gpu->device->get(*readback).read(pixels.data(), byteSize);
    m_gpu->device->free(*readback);
    if (view)
    {
        // RGBA16F to RGBA8, rounded to the nearest.
        std::vector<std::uint16_t> halves(byteSize / sizeof(std::uint16_t));
        std::memcpy(halves.data(), pixels.data(), byteSize);
        pixels.resize(halves.size());
        std::ranges::transform(halves, pixels.begin(), [](std::uint16_t half) {
            return static_cast<std::byte>(std::lround(std::clamp(math::halfToFloat(half), 0.0f, 1.0f) * 255.0f));
        });
    }
    return pixels;
}

auto Engine::readView(std::string_view name) -> std::expected<std::vector<std::byte>, std::string>
{
    const auto& targets = m_presentation->views();
    const auto view = std::ranges::find(targets, name, &ViewTarget::name);
    if (view == targets.end())
        return std::unexpected{ std::format("the frame graph renders no view '{}'", name) };
    m_gpu->device->waitIdle();
    return readBack(m_presentation->frameGraph().pool().texture(view->final), view->extent, true);
}

// Reads the offscreen target back (the last frame's slot), the whole frame with the shell's UI over it, or with
// AppDesc::screenshotView each view's target, and writes it as PNG. The device is idle.
auto Engine::saveScreenshot() -> std::expected<void, std::string>
{
    // The target's format also decides the view's encoding (PostProcessingPass): RGBA8 gets it gamma encoded.
    const auto& renderer = *m_gpu->renderer;
    if (!renderer.offscreen() || renderer.targetFormat() != rhi::Format::RGBA8_UNORM)
        return std::unexpected{ "screenshots need the headless RGBA8 target" };

    const auto write = [](const std::string& path, rhi::Extent2D extent, const std::vector<std::byte>& pixels) -> std::expected<void, std::string> {
        constexpr int channels{ 4 };
        if (!stbi_write_png(path.c_str(), static_cast<int>(extent.x), static_cast<int>(extent.y), channels, pixels.data(),
                static_cast<int>(extent.x) * channels))
            return std::unexpected{ std::format("failed to write '{}'", path) };
        log::info("Screenshot saved: {}", path);
        return {};
    };

    const auto& targets = m_presentation->views();
    if (targets.empty() || !m_desc.screenshotView)
    {
        const auto pixels = readBack(renderer.offscreenTarget(m_lastFrameIndex), renderer.extent(), false);
        if (!pixels)
            return std::unexpected{ pixels.error() };
        return write(m_desc.screenshot, renderer.extent(), *pixels);
    }
    // One view's goes to the path; several go next to it, each named after its view.
    const std::filesystem::path path{ m_desc.screenshot };
    for (const auto& view : targets)
    {
        const auto pixels = readBack(m_presentation->frameGraph().pool().texture(view.final), view.extent, true);
        if (!pixels)
            return std::unexpected{ pixels.error() };
        const auto viewPath = targets.size() == 1
                                  ? path
                                  : path.parent_path() / std::format("{}-{}{}", path.stem().string(), view.name, path.extension().string());
        if (auto written = write(viewPath.string(), view.extent, *pixels); !written)
            return written;
    }
    return {};
}
}
