module;

#include <cassert>
#include <cstdlib>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

module aegis.engine;

import std;
import aegis.paths;
import aegis.project_file;
import aegis.render_passes;
import aegis.scripting;

namespace aegis
{
namespace
{
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
constexpr float HEADLESS_FRAME_DELTA_SEC{ 1.0f / 60.0f }; // Fixed, so a headless run does not depend on the machine

auto makeError(EngineErrorCode code, std::string message) -> EngineError
{
    return EngineError{ .code = code, .message = std::move(message) };
}

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
        return std::unexpected{ makeError(EngineErrorCode::SceneFailed, std::move(created.error())) };
    return {};
}

void Engine::newScene()
{
    resetWorld();
}

void Engine::setTitle(std::string_view title)
{
    if (m_window)
        m_window->setTitle(title);
}

void Engine::restoreCursor()
{
    if (m_input)
        m_input->setCursorMode(platform::CursorMode::Normal);
}

auto Engine::contentScale() const -> float
{
    return m_window ? m_window->contentScale() : 1.0f;
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
    // The engine's systems, always present. Their phases order them (scripts, then transforms,
    // then cameras); the game's come after them (setWorldSetup), in the phase they declare.
    if (m_input)
        world.addSystem<scripting::ScriptSystem>(m_input->state(), m_input.get());
    else
        world.addSystem<scripting::ScriptSystem>(m_headlessInput); // Scripts see the input map, and no input
    world.addSystem<world::TransformSystem>();
    world.addSystem<world::CameraSystem>();
    if (m_worldSetup)
        m_worldSetup(world);
}

void Engine::resetWorld()
{
    m_worlds.reset(world());
}

auto buildFrameGraph(render_graph::GraphBuilder& graph, const render_graph::DefaultPassesDesc& desc, const GraphBuild& game,
    Shell& shell) -> void
{
    auto viewDesc = desc;
    const auto shared = render_graph::addSharedPasses(graph, desc);
    viewDesc.instances = shared.instances;
    viewDesc.lights = shared.lights;
    for (const auto view : render_graph::viewDeclarationOrder(desc.views))
    {
        graph.beginScope(desc.views[view].name, desc.views[view].extent);
        viewDesc.view = view;
        if (desc.views[view].kind == render_graph::ViewKind::Shadow)
            render_graph::addShadowPasses(graph, viewDesc);
        else
        {
            if (game)
                game(graph, viewDesc);
            else
                render_graph::addScenePasses(graph, viewDesc);
            shell.buildGraph(graph, viewDesc);
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
                          .and_then([this] { return scripting::registerKinematicMovementController(m_types); });
    if (!registered)
        return std::unexpected{ makeError(EngineErrorCode::TypesFailed, std::format("Failed to register the built-in types: {}", reflect::describe(registered.error()))) };
    if (auto created = createWindow(); !created)
        return created;
    if (auto loaded = loadProjectFile(); !loaded)
        return loaded;
    if (auto created = createRenderer(); !created)
        return created;
    if (auto created = createAssets(); !created)
        return created;
    m_mainWorld = &createWorld(); // After the input, which its scripts read

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
    // The GPU may still use what the members below free. The shell ends first, while the worlds, the
    // frame graph and ImGui still exist; the members then go bottom up (the UI pass before the ImGui
    // context). A failed create() leaves the later members empty.
    if (m_device)
        m_device->waitIdle();
    m_shell->onShutdown();
    m_shell.reset();
    // Its registrations are freed through the UI pass, and its sampler through the device.
    m_presentation.destroyTextures();
}

auto Engine::useGpuDrivenRendering() const -> bool
{
    return m_desc.gpuDriven && m_device->capabilities().meshShaders;
}

auto Engine::run() -> int
{
    const bool headless = !m_window;
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
    if (auto created = createFrameGraph(); !created)
    {
        log::fatal("{}", describe(created.error()));
        return exitFailure;
    }

    if (headless)
        m_clock = core::FrameClock::fixed(HEADLESS_FRAME_DELTA_SEC);
    resetClocks(std::chrono::steady_clock::now());
    while (!m_quit && (headless || !m_window->shouldClose()) && (m_desc.frames == 0 || frames < m_desc.frames))
    {
        ScopeProfiler frameTime("Frame Time");

        const auto currentFrameBegin = std::chrono::steady_clock::now();

        if (!headless)
        {
            m_window->update();
            if (m_window->isMinimized())
            {
                m_window->waitEvents();
                resetClocks(std::chrono::steady_clock::now()); // Not part of the next frame's delta
                continue;
            }
            if (m_window->wasResized() || m_renderer->needsResize())
            {
                if (!resize())
                {
                    exitCode = exitFailure;
                    break;
                }
            }

            // The shell decides what the game doesn't see, from the UI it built last frame.
            captureInput();
            m_input->update();
        }
        // The clock clamps a long frame (a breakpoint, a load) so the UI doesn't get the whole stall; so do the worlds'.
        const float frameTimeSec = m_clock.tick(currentFrameBegin);
        // The graph follows the views the shell's UI asked for last frame, before this frame's UI shows their textures.
        if (!followViews(m_shell->renderViews()))
        {
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

        // Rendering: uploads queued this frame (or while loading) go first, on the same queue.
        if (auto flushed = m_device->flushUploads(); !flushed)
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
        const auto boundScene = m_frameScene.bind(scene);
        // What recording can't do without (the draw batches' pipelines, buffers grown to the frame's instances and
        // lights) is created before it; failing that ends the run, as a failed frame does.
        if (auto prepared = m_frameGraph->prepare(); !prepared)
        {
            log::fatal("Failed to prepare a frame: {}", render_graph::describe(prepared.error()));
            exitCode = exitFailure;
            break;
        }
        auto rendered = m_renderer->renderFrame([this, &instances](const aegis::renderer::Renderer::FrameInfo& frame) {
            m_lastFrameIndex = frame.frameIndex;
            m_frameGraph->execute({
                .cmd = frame.cmd,
                .frameIndex = frame.frameIndex,
                .extent = frame.extent,
                .target = frame.target,
            });
            // Recorded: the next frame needs only what changes after this one.
            instances.clearDirty();
        });
        if (!rendered)
        {
            log::fatal("Failed to render a frame: {}", aegis::renderer::describe(rendered.error()));
            exitCode = exitFailure;
            break;
        }
        // Frame Time less this is the CPU's time (the editor's status bar)
        Profiler::instance().addTime("Frame Wait", m_renderer->waitMs());
        ++frames;
    }

    m_device->waitIdle();

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

auto Engine::createWindow() -> std::expected<void, EngineError>
{
    if (m_desc.headless)
        return {};

    auto window = platform::Window::create({ .title = m_desc.title, .width = m_desc.width, .height = m_desc.height });
    if (!window)
        return std::unexpected{ makeError(EngineErrorCode::WindowFailed, std::format("Failed to create the window: {}", platform::describe(window.error()))) };
    m_window = std::make_unique<platform::Window>(std::move(*window));
    m_input = std::make_unique<platform::Input>(*m_window);
    m_window->setCloseHandler([this] { return m_shell->canClose(); });
    m_presentation.attach(*m_window); // After the input: ImGui's callbacks chain to the window's
    return {};
}

auto Engine::loadProjectFile() -> std::expected<void, EngineError>
{
    auto file = aegis::loadProjectFile(m_desc.project.empty() ? paths::project() : m_desc.project);
    if (!file)
        return std::unexpected{ makeError(EngineErrorCode::ProjectFailed, std::format("Failed to read the project file {}", file.error())) };
    if (m_input)
        m_input->setMap(std::move(file->input));
    else
        m_headlessInput.setMap(std::move(file->input));
    return {};
}

auto Engine::createRenderer() -> std::expected<void, EngineError>
{
    auto context = aegis::rhi::Context::create({ .appName = m_desc.title.c_str(), .window = m_window.get() });
    if (!context)
        return std::unexpected{ makeError(EngineErrorCode::DeviceFailed, std::format("Failed to create the Vulkan context ({})", aegis::rhi::describe(context.error()))) };
    m_context = std::move(*context);

    auto device = m_context->createDevice({});
    if (!device)
        return std::unexpected{ makeError(EngineErrorCode::DeviceFailed, std::format("Failed to create the Vulkan device ({})", aegis::rhi::describe(device.error()))) };
    m_device = std::move(*device);

    auto renderer = aegis::renderer::Renderer::create({
        .context = *m_context,
        .device = *m_device,
        .window = m_window.get(),
        .offscreen = { .extent = { m_desc.width, m_desc.height } },
    });
    if (!renderer)
        return std::unexpected{ makeError(EngineErrorCode::RendererFailed, std::format("Failed to create the renderer: {}", aegis::renderer::describe(renderer.error()))) };
    m_renderer = std::move(*renderer);

    if (auto created = m_presentation.createTextures(*m_device); !created)
        return std::unexpected{ makeError(EngineErrorCode::RendererFailed, std::format("Failed to create the UI textures: {}", created.error())) };
    return {};
}

auto Engine::createAssets() -> std::expected<void, EngineError>
{
    auto assets = AssetLibrary::create(*m_device, FRAMES_IN_FLIGHT, assetSources(m_desc));
    if (!assets)
        return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, assets.error()) };
    m_assets.emplace(std::move(*assets));
    return {};
}

auto Engine::createFrameGraph(std::vector<ViewTarget> views) -> std::expected<void, EngineError>
{
    using aegis::render_graph::DefaultPassesDesc;

    // The UI pass owns ImGui's Vulkan backend, of which there is one: the old graph goes first. Its shutdown takes
    // ImGui's platform windows along, so the GLFW backend starts again too.
    if (m_frameGraph)
    {
        m_frameGraph.reset();
        if (m_window)
        {
            m_presentation.detach();
            m_presentation.attach(*m_window);
        }
    }
    m_views = std::move(views);
    m_frameGraph = std::make_unique<aegis::render_graph::FrameGraph>(*m_device, m_renderer->extent());
    DefaultPassesDesc desc{
        .scene = m_frameScene,
        .gpuDriven = useGpuDrivenRendering(),
        .ui = true, // The shell's UI, empty for a game; headless too, for screenshots of it
        .context = m_context.get(),
    };
    // Without views of its own the main camera renders onto the target, following it.
    if (!m_views.empty())
    {
        desc.views.clear();
        for (const auto& view : m_views)
            desc.views.push_back({ .name = view.name, .extent = view.extent });
        desc.shownViews = DefaultPassesDesc::ShownViews{ .targetFormat = m_renderer->targetFormat() };
    }
    // Each main view samples a shadow view of the directional light; frameScene adds the same views to the scene.
    render_graph::addShadowViews(desc.views);
    buildFrameGraph(*m_frameGraph, desc, m_graphBuild, *m_shell);
    if (auto compiled = m_frameGraph->compile(); !compiled)
        return std::unexpected{ makeError(EngineErrorCode::FrameGraphFailed, std::format("Failed to compile the frame graph: {}", describe(compiled.error()))) };
    for (auto& view : m_views)
    {
        const auto final = m_frameGraph->declarations().find(render_graph::scopedName(view.name, render_graph::names::finalImage));
        if (!final)
            return std::unexpected{ makeError(EngineErrorCode::FrameGraphFailed, std::format("The frame graph's view '{}' has no \"Final\"", view.name)) };
        view.final = *final;
    }
    return {};
}

bool Engine::followViews(const std::vector<RenderView>& views)
{
    std::vector<ViewTarget> wanted;
    for (const auto& view : views)
    {
        auto name = view.name.empty() ? std::string{ render_graph::names::mainView } : view.name;
        if (name.contains('/') || std::ranges::contains(wanted, name, &ViewTarget::name))
        {
            log::fatal("The shell's view name '{}' is used twice or has a '/'", name);
            return false;
        }
        wanted.push_back({ .name = std::move(name), .extent = { std::max(view.extent.x, 1u), std::max(view.extent.y, 1u) } });
    }
    const bool sameViews = std::ranges::equal(wanted, m_views, {}, &ViewTarget::name, &ViewTarget::name);
    if (sameViews && std::ranges::equal(wanted, m_views, {}, &ViewTarget::extent, &ViewTarget::extent))
        return true;

    // Frames in flight still use the graph's textures (and, rebuilding, its passes), the UI's registrations too.
    m_device->waitIdle();
    m_presentation.releaseTextures();
    if (!sameViews)
    {
        if (auto created = createFrameGraph(std::move(wanted)); !created)
        {
            log::fatal("{}", describe(created.error()));
            return false;
        }
        return true;
    }
    // The same views, some at a new extent: only their textures are recreated.
    for (std::size_t i = 0; i < wanted.size(); ++i)
    {
        if (wanted[i].extent == m_views[i].extent)
            continue;
        m_views[i].extent = wanted[i].extent;
        if (auto resized = m_frameGraph->resized(m_views[i].name, m_views[i].extent); !resized)
        {
            log::fatal("Failed to resize the view '{}': {}", m_views[i].name, describe(resized.error()));
            return false;
        }
    }
    return true;
}

bool Engine::resize()
{
    if (auto resized = m_renderer->resize(aegis::rhi::Extent2D{ m_window->extent() }); !resized)
    {
        log::fatal("Failed to resize the renderer: {}", aegis::renderer::describe(resized.error()));
        return false;
    }
    m_window->resetResized();
    if (!m_views.empty())
        return true; // The graph follows the views, not the window
    // The renderer waited for the frames in flight; the graph's views are about to be recreated.
    m_presentation.releaseTextures();
    if (auto resized = m_frameGraph->resized(m_renderer->extent()); !resized)
        log::fatal("Failed to resize the frame graph: {}", describe(resized.error()));
    return true;
}

void Engine::captureInput()
{
    const auto capture = m_shell->inputCapture();
    m_input->setCaptured(capture.keyboard, capture.mouse);
}

void Engine::runShell(float deltaSeconds)
{
    m_updates.clear();
    m_renderRequest = nullptr;
    m_presentation.beginFrame(m_renderer->extent(), deltaSeconds);
    FrameContext frame{
        .engine = *this,
        .world = world(),
        .types = m_types,
        .assets = *m_assets,
        .frameGraph = *m_frameGraph,
        .views = m_views,
        .presentation = m_presentation,
        .renderer = *m_renderer,
        .deltaSeconds = deltaSeconds,
    };
    m_shell->onFrame(frame);
    m_presentation.endFrame();
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
    auto extent = m_window ? rhi::Extent2D{ m_window->extent() } : m_renderer->extent();
    if (!m_views.empty())
    {
        const auto throughMain = std::ranges::find_if(m_views, [&](const ViewTarget& target) {
            const auto view = std::ranges::find_if(views, [&](const RenderView& each) {
                return (each.name.empty() ? render_graph::names::mainView : std::string_view{ each.name }) == target.name;
            });
            return view == views.end() || !view->camera;
        });
        extent = throughMain != m_views.end() ? throughMain->extent : m_views.front().extent;
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

auto Engine::frameScene(world::World& shown, const render_graph::InstanceStore& instances, const std::vector<RenderView>& views) const
    -> render_graph::FrameScene
{
    auto scene = aegis::extractFrameScene(shown, instances);
    if (m_views.empty())
    {
        render_graph::addShadowViews(scene); // As createFrameGraph's
        return scene;                        // The main camera's view, onto the target
    }
    // The views the graph renders, in its order; a shell view asked for this frame gives its camera.
    const auto mainCamera = scene.camera(0);
    scene.views.clear();
    for (const auto& target : m_views)
    {
        const auto view = std::ranges::find_if(views, [&](const RenderView& each) {
            return (each.name.empty() ? render_graph::names::mainView : std::string_view{ each.name }) == target.name;
        });
        scene.views.push_back({
            .name = target.name,
            .camera = view != views.end() && view->camera ? view->camera : mainCamera,
            .extent = target.extent,
        });
    }
    render_graph::addShadowViews(scene); // As createFrameGraph's
    return scene;
}

auto Engine::readBack(const renderer::Texture& image, rhi::Extent2D extent, bool view) -> std::expected<std::vector<std::byte>, std::string>
{
    // A view's target is its "Final" (RGBA16F), left sampled by the UI pass; the offscreen target a color attachment.
    const auto state = view ? rhi::ResourceState::ShaderReadFragment : rhi::ResourceState::ColorAttachment;
    const std::size_t texelSize{ view ? 4 * sizeof(std::uint16_t) : 4 };
    const auto byteSize = std::size_t{ extent.x } * extent.y * texelSize;

    auto readback = m_device->createBuffer({
        .name = "ScreenshotReadback",
        .size = byteSize,
        .usage = rhi::BufferUsage::TransferDst,
        .memory = rhi::MemoryUsage::CpuRead,
    });
    if (!readback)
        return std::unexpected{ "failed to create the readback buffer" };

    auto pool = m_device->createCommandPool({ .name = "ScreenshotReadback", .queueFamily = m_device->graphicsQueue().family() });
    if (!pool)
        return std::unexpected{ "failed to create the command pool" };
    auto cmd = m_device->createCommandBuffer({ .name = "ScreenshotReadback", .pool = *pool });
    if (!cmd)
        return std::unexpected{ "failed to create the command buffer" };

    // The image goes back to the state it was in, so the next frame's barriers still hold.
    const std::array regions{ rhi::BufferImageCopy{ .extent = rhi::Extent3D{ extent } } };
    cmd->begin(true);
    cmd->transitionImageLayout(image.view, state, rhi::ResourceState::CopySrc);
    cmd->copyImageToBuffer(image.image, m_device->get(*readback), regions);
    cmd->transitionImageLayout(image.view, rhi::ResourceState::CopySrc, state);
    cmd->end();
    auto submitted = m_device->graphicsQueue().submit(*cmd);
    if (!submitted)
        return std::unexpected{ "failed to submit the readback" };
    m_device->graphicsQueue().wait(*submitted);

    std::vector<std::byte> pixels(byteSize);
    m_device->get(*readback).read(pixels.data(), byteSize);
    m_device->free(*readback);
    if (view)
    {
        // RGBA16F to RGBA8, rounded to the nearest.
        std::vector<std::uint16_t> halves(byteSize / sizeof(std::uint16_t));
        std::memcpy(halves.data(), pixels.data(), byteSize);
        pixels.resize(halves.size());
        std::ranges::transform(halves, pixels.begin(), [](std::uint16_t half) {
            return static_cast<std::byte>(std::lround(std::clamp(glm::unpackHalf1x16(half), 0.0f, 1.0f) * 255.0f));
        });
    }
    return pixels;
}

auto Engine::readView(std::string_view name) -> std::expected<std::vector<std::byte>, std::string>
{
    const auto view = std::ranges::find(m_views, name, &ViewTarget::name);
    if (view == m_views.end())
        return std::unexpected{ std::format("the frame graph renders no view '{}'", name) };
    m_device->waitIdle();
    return readBack(m_frameGraph->pool().texture(view->final), view->extent, true);
}

// Reads the offscreen target back (the last frame's slot), the whole frame with the shell's UI over it, or with
// AppDesc::screenshotView each view's target, and writes it as PNG. The device is idle.
auto Engine::saveScreenshot() -> std::expected<void, std::string>
{
    // The target's format also decides the view's encoding (PostProcessingPass): RGBA8 gets it gamma encoded.
    if (!m_renderer->offscreen() || m_renderer->targetFormat() != rhi::Format::RGBA8_UNORM)
        return std::unexpected{ "screenshots need the headless RGBA8 target" };

    const auto write = [](const std::string& path, rhi::Extent2D extent, const std::vector<std::byte>& pixels) -> std::expected<void, std::string> {
        constexpr int channels{ 4 };
        if (!stbi_write_png(path.c_str(), static_cast<int>(extent.x), static_cast<int>(extent.y), channels, pixels.data(),
                static_cast<int>(extent.x) * channels))
            return std::unexpected{ std::format("failed to write '{}'", path) };
        log::info("Screenshot saved: {}", path);
        return {};
    };

    if (m_views.empty() || !m_desc.screenshotView)
    {
        const auto pixels = readBack(m_renderer->offscreenTarget(m_lastFrameIndex), m_renderer->extent(), false);
        if (!pixels)
            return std::unexpected{ pixels.error() };
        return write(m_desc.screenshot, m_renderer->extent(), *pixels);
    }
    // One view's goes to the path; several go next to it, each named after its view.
    const std::filesystem::path path{ m_desc.screenshot };
    for (const auto& view : m_views)
    {
        const auto pixels = readBack(m_frameGraph->pool().texture(view.final), view.extent, true);
        if (!pixels)
            return std::unexpected{ pixels.error() };
        const auto viewPath = m_views.size() == 1
                                  ? path
                                  : path.parent_path() / std::format("{}-{}{}", path.stem().string(), view.name, path.extension().string());
        if (auto written = write(viewPath.string(), view.extent, *pixels); !written)
            return written;
    }
    return {};
}
}
