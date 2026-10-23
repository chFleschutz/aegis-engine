module;

#include <cassert>
#include <cstdlib>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

module aegis.engine;

import std;
import aegis.render_passes;

namespace aegis
{
namespace
{
constexpr std::uint32_t DEFAULT_HEADLESS_FRAMES{ 600 };
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
        if (arg == "--headless")
        {
            desc.headless = true;
        }
        else if (arg == "--screenshot" && hasValue)
        {
            desc.screenshot = args[++i];
        }
        else if (arg == "--project" && hasValue)
        {
            desc.project = args[++i]; // argv is in the native narrow encoding, as path expects
        }
        else if (arg == "--scene" && hasValue)
        {
            desc.scene = args[++i];
        }
        else if (arg == "--dump-asset" && hasValue)
        {
            desc.dumpAsset = args[++i];
        }
        else if ((arg == "--frames" || arg == "--width" || arg == "--height") && hasValue)
        {
            const auto count = parseCount(arg, args[++i]);
            if (!count)
                return std::unexpected{ count.error() };
            auto& target = arg == "--frames" ? desc.frames : arg == "--width" ? desc.width
                                                                              : desc.height;
            target = *count;
        }
        else
        {
            return std::unexpected{ makeError(EngineErrorCode::InvalidArguments, std::format("Unknown or incomplete argument '{}'", arg)) };
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

auto usage(std::string_view program) -> std::string
{
    return std::format("Usage: {} [--headless] [--frames <n>] [--screenshot <png>] [--width <n>] [--height <n>] [--project <dir>] [--scene <path>] [--dump-asset <id>]",
        program);
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

    // A mesh is a part of its model file's entry.
    auto model = *id;
    std::string_view meshKey;
    if (!entry->key.empty())
    {
        const auto owner = database->find(entry->root, entry->path);
        if (entry->type != "StaticMesh" || !owner)
            return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, std::format("{} is '{}' of '{}'; only models and their meshes are cached", *id, entry->key, entry->path)) };
        model = *owner;
        meshKey = entry->key;
    }
    else if (assets::importerFor(entry->path) != "model")
    {
        return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, std::format("{} is '{}'; only models and their meshes are cached", *id, entry->path)) };
    }

    const auto* modelEntry = database->find(model);
    auto text = assets::ImportCache{ sources.importCache }.dump(database->absolutePath(*modelEntry), modelEntry->settings, meshKey);
    if (!text)
        return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, assets::describe(text.error())) };
    return std::move(*text);
}

auto createEngine(int argc, char** argv) -> std::expected<std::unique_ptr<Engine>, int>
{
    const auto desc = parseArgs(argc, argv);
    if (!desc)
    {
        log::error("{}\n{}", describe(desc.error()), usage(argc > 0 ? argv[0] : "app"));
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

auto Engine::loadScene(assets::AssetId scene) -> std::expected<void, EngineError>
{
    resetWorld();
    if (auto created = m_assets->instantiateScene(world(), m_types, scene); !created)
        return std::unexpected{ makeError(EngineErrorCode::SceneFailed, std::move(created.error())) };
    return {};
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
        world.addSystem<scripting::ScriptSystem>(world.registry(), m_input->state(), m_input.get());
    else
        world.addSystem<scripting::ScriptSystem>(world.registry()); // Scripts see no input
    world.addSystem<world::TransformSystem>();
    world.addSystem<world::CameraSystem>();
    if (m_worldSetup)
        m_worldSetup(world);
}

void Engine::resetWorld()
{
    m_worlds.reset(world());
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
    if (auto created = createRenderer(); !created)
        return created;
    if (auto created = createAssets(); !created)
        return created;
    m_mainWorld = &createWorld(); // After the input, which its scripts read
    if (auto created = createFrameGraph(); !created)
        return created;

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
    return ENABLE_GPU_DRIVEN_RENDERING && m_desc.gpuDriven && m_device->capabilities().meshShaders;
}

auto Engine::run() -> int
{
    const bool headless = !m_window;
    std::uint32_t frames{ 0 };
    int exitCode{ exitSuccess };

    if (auto started = m_shell->onStartup(*this); !started)
    {
        log::fatal("The shell failed to start: {}", describe(started.error()));
        return exitFailure;
    }

    if (headless)
        m_clock = core::FrameClock::fixed(HEADLESS_FRAME_DELTA_SEC);
    resetClocks(std::chrono::steady_clock::now());
    while ((headless || !m_window->shouldClose()) && (m_desc.frames == 0 || frames < m_desc.frames))
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
            const auto capture = m_shell->inputCapture();
            m_input->setCaptured(capture.keyboard, capture.mouse);
            m_input->update();
        }
        // The clock clamps a long frame (a breakpoint, a load) so the UI doesn't get the whole stall; so do the worlds'.
        const float frameTimeSec = m_clock.tick(currentFrameBegin);
        // The graph follows the view the shell's UI asked for last frame, before this frame's UI shows its textures.
        if (!followView(m_shell->renderView()))
        {
            exitCode = exitFailure;
            break;
        }
        // The safe point: the shell's UI and edits come before the update, so an edit shows this frame. The shell
        // also picks the worlds that update and the one that renders.
        runShell(frameTimeSec);
        // The camera the shell's UI asks for this frame; every world's main camera gets the graph's aspect.
        const auto view = m_shell->renderView();
        updateCameraAspect(m_viewExtent.value_or(m_window ? rhi::Extent2D{ m_window->extent() } : m_renderer->extent()));
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
        auto frameScene = aegis::extractFrameScene(shown, instances);
        if (view && view->camera)
            frameScene.camera = view->camera;
        auto rendered = m_renderer->renderFrame([this, &frameScene, &instances](const aegis::renderer::Renderer::FrameInfo& frame) {
            m_lastFrameIndex = frame.frameIndex;
            m_frameGraph->execute({
                .cmd = frame.cmd,
                .frameScene = frameScene,
                .frameIndex = frame.frameIndex,
                .extent = m_viewExtent.value_or(frame.extent), // The scene passes' extent: the graph's output
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
    m_presentation.attach(*m_window); // After the input: ImGui's callbacks chain to the window's
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

auto Engine::createFrameGraph(std::optional<rhi::Extent2D> view) -> std::expected<void, EngineError>
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
    m_viewExtent = view;
    m_frameGraph = std::make_unique<aegis::render_graph::FrameGraph>(*m_device, view.value_or(m_renderer->extent()));
    aegis::render_graph::addDefaultPasses(*m_frameGraph,
        {
            .gpuDriven = useGpuDrivenRendering(),
            .ui = true, // The shell's UI, empty for a game; headless too, for screenshots of it
            .context = m_context.get(),
            .view = view ? std::optional{ DefaultPassesDesc::View{ .targetFormat = m_renderer->targetFormat() } } : std::nullopt,
        });
    if (auto compiled = m_frameGraph->compile(); !compiled)
        return std::unexpected{ makeError(EngineErrorCode::FrameGraphFailed, std::format("Failed to compile the frame graph: {}", describe(compiled.error()))) };
    return {};
}

bool Engine::followView(const std::optional<RenderView>& view)
{
    const auto extent = view ? std::optional{ rhi::Extent2D{ std::max(view->extent.x, 1u), std::max(view->extent.y, 1u) } } : std::nullopt;
    if (extent == m_viewExtent)
        return true;

    // Frames in flight still use the graph's textures (and, rebuilding, its passes), the UI's registrations too.
    m_device->waitIdle();
    m_presentation.releaseTextures();
    if (extent.has_value() != m_viewExtent.has_value())
    {
        if (auto created = createFrameGraph(extent); !created)
        {
            log::fatal("{}", describe(created.error()));
            return false;
        }
        return true;
    }
    m_viewExtent = extent;
    if (auto resized = m_frameGraph->resized(*extent); !resized)
    {
        log::fatal("Failed to resize the view: {}", describe(resized.error()));
        return false;
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
    if (m_viewExtent)
        return true; // The graph follows the view, not the window
    // The renderer waited for the frames in flight; the graph's views are about to be recreated.
    m_presentation.releaseTextures();
    if (auto resized = m_frameGraph->resized(m_renderer->extent()); !resized)
        log::fatal("Failed to resize the frame graph: {}", describe(resized.error()));
    return true;
}

void Engine::runShell(float deltaSeconds)
{
    m_updates.clear();
    m_renderRequest = nullptr;
    m_presentation.beginFrame(m_renderer->extent(), deltaSeconds);
    FrameContext frame{
        .engine = *this,
        .world = world(),
        .frameGraph = *m_frameGraph,
        .viewExtent = m_viewExtent,
        .presentation = m_presentation,
        .instances = m_worlds.instances(world()),
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

void Engine::updateCameraAspect(rhi::Extent2D extent)
{
    if (extent.y == 0)
        return;
    const auto aspect = static_cast<float>(extent.x) / static_cast<float>(extent.y);
    for (auto* each : m_worlds.all())
    {
        auto& registry = each->registry();
        if (const auto mainCamera = each->mainCamera(); mainCamera && registry.has<Camera>(mainCamera))
            registry.get<Camera>(mainCamera).aspect = aspect;
    }
}

// Reads the offscreen target back (the last frame's slot), or with a view the view's target, and writes it as PNG.
// The device is idle.
auto Engine::saveScreenshot() -> std::expected<void, std::string>
{
    // The target's format also decides the view's encoding (PostProcessingPass): RGBA8 gets it gamma encoded.
    if (!m_renderer->offscreen() || m_renderer->targetFormat() != rhi::Format::RGBA8_UNORM)
        return std::unexpected{ "screenshots need the headless RGBA8 target" };

    // A view's target is the graph's "Final" (RGBA16F), left sampled by the UI pass.
    const auto final = m_frameGraph->declarations().find(render_graph::names::finalImage);
    if (m_viewExtent && !final)
        return std::unexpected{ "the view's frame graph has no \"Final\"" };
    const auto& target = m_viewExtent ? m_frameGraph->pool().texture(*final) : m_renderer->offscreenTarget(m_lastFrameIndex);
    const auto state = m_viewExtent ? rhi::ResourceState::ShaderReadFragment : rhi::ResourceState::ColorAttachment;
    const auto extent = m_viewExtent.value_or(m_renderer->extent());
    const std::size_t texelSize{ m_viewExtent ? 4 * sizeof(std::uint16_t) : 4 };
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

    const std::array regions{ rhi::BufferImageCopy{ .extent = rhi::Extent3D{ extent } } };
    cmd->begin(true);
    cmd->transitionImageLayout(target.view, state, rhi::ResourceState::CopySrc);
    cmd->copyImageToBuffer(target.image, m_device->get(*readback), regions);
    cmd->end();
    auto submitted = m_device->graphicsQueue().submit(*cmd);
    if (!submitted)
        return std::unexpected{ "failed to submit the readback" };
    m_device->graphicsQueue().wait(*submitted);

    std::vector<std::byte> pixels(byteSize);
    m_device->get(*readback).read(pixels.data(), byteSize);
    m_device->free(*readback);
    if (m_viewExtent)
    {
        // RGBA16F to RGBA8, rounded to the nearest.
        std::vector<std::uint16_t> halves(byteSize / sizeof(std::uint16_t));
        std::memcpy(halves.data(), pixels.data(), byteSize);
        pixels.resize(halves.size());
        std::ranges::transform(halves, pixels.begin(), [](std::uint16_t half) {
            return static_cast<std::byte>(std::lround(std::clamp(glm::unpackHalf1x16(half), 0.0f, 1.0f) * 255.0f));
        });
    }

    constexpr int channels{ 4 };
    if (!stbi_write_png(m_desc.screenshot.c_str(), static_cast<int>(extent.x), static_cast<int>(extent.y), channels,
            pixels.data(), static_cast<int>(extent.x) * channels))
        return std::unexpected{ std::format("failed to write '{}'", m_desc.screenshot) };
    log::info("Screenshot saved: {}", m_desc.screenshot);
    return {};
}
}
