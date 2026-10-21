module;

#include "core/assert.h"

#include <cstdlib>

#include <imgui.h>
#include <imgui_impl_glfw.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

module aegis.engine;

import std;

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

auto createEngine(int argc, char** argv) -> std::expected<std::unique_ptr<Engine>, int>
{
    const auto desc = parseArgs(argc, argv);
    if (!desc)
    {
        log::error("{}\nUsage: {} [--headless] [--frames <n>] [--screenshot <png>] [--width <n>] [--height <n>]",
            describe(desc.error()), argc > 0 ? argv[0] : "app");
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

auto Engine::loadScene(assets::AssetId scene) -> std::expected<void, EngineError>
{
    resetWorld();
    if (auto created = m_assets->instantiateScene(m_world, m_types, scene); !created)
        return std::unexpected{ makeError(EngineErrorCode::SceneFailed, std::move(created.error())) };
    beginWorld();
    return {};
}

void Engine::resetWorld()
{
    m_world.reset();
    // The engine's systems, always present. Their phases order them (scripts, then transforms,
    // then cameras); a game adds its own systems the same way, in the phase they declare.
    if (m_input)
        m_world.addSystem<scripting::ScriptSystem>(m_world.registry(), m_input->state(), m_input.get());
    else
        m_world.addSystem<scripting::ScriptSystem>(m_world.registry()); // Scripts see no input
    m_world.addSystem<world::TransformSystem>();
    m_world.addSystem<world::CameraSystem>();
}

void Engine::beginWorld()
{
    m_world.begin();
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
    // The GPU may still use what the members below free; ImGui's backends go before its context.
    // A failed create() leaves the later members empty.
    if (m_device)
        m_device->waitIdle();
    m_frameGraph.reset();
    if (m_imguiGlfw)
        ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

auto Engine::useGpuDrivenRendering() const -> bool
{
    return ENABLE_GPU_DRIVEN_RENDERING && m_device->capabilities().meshShaders;
}

auto Engine::run() -> int
{
    const bool headless = !m_window;
    std::uint32_t frames{ 0 };
    int exitCode{ exitSuccess };

    if (headless)
        m_clock = base::FrameClock::fixed(HEADLESS_FRAME_DELTA_SEC);
    m_clock.reset(std::chrono::steady_clock::now());
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
                m_clock.reset(std::chrono::steady_clock::now()); // Not part of the next frame's delta
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

            // Update: the systems run in order, scripts first: they write the transforms, the others derive
            // from them (camera matrices included), then the layers (the editor) read the result.
            // ImGui's flags are from the previous frame's UI; they gate the game input.
            const auto& io = ImGui::GetIO();
            m_input->setCaptured(io.WantCaptureKeyboard, io.WantCaptureMouse);
            m_input->update();
        }
        updateCameraAspect();
        // The clock clamps a long frame (a breakpoint, a load) so scripts and systems don't get the whole stall
        const float frameTimeSec = m_clock.tick(currentFrameBegin);
        m_world.update(frameTimeSec);
        m_layerStack.update(frameTimeSec);

        // Rendering: uploads queued this frame (or while loading) go first, on the same queue.
        if (auto flushed = m_device->flushUploads(); !flushed)
        {
            log::fatal("Failed to flush uploads: {}", rhi::describe(flushed.error()));
            exitCode = exitFailure;
            break;
        }
        // The passes draw the instances, camera, lights and sky as the update left them.
        m_instanceBridge.sync();
        const auto frameScene = aegis::extractFrameScene(m_world, m_instances);
        auto rendered = m_renderer->renderFrame([this, &frameScene](const aegis::renderer::Renderer::FrameInfo& frame) {
            m_lastFrameIndex = frame.frameIndex;
            m_frameGraph->execute({
                .cmd = frame.cmd,
                .frameScene = frameScene,
                .frameIndex = frame.frameIndex,
                .extent = frame.extent,
                .target = frame.target,
            });
            // Recorded: the next frame needs only what changes after this one.
            m_instances.clearDirty();
        });
        if (!rendered)
        {
            log::fatal("Failed to render a frame: {}", aegis::renderer::describe(rendered.error()));
            exitCode = exitFailure;
            break;
        }
        ++frames;

        if (!headless)
            applyFrameBrake(currentFrameBegin);
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
    return {};
}

auto Engine::createAssets() -> std::expected<void, EngineError>
{
    auto assets = AssetLibrary::create(*m_device, FRAMES_IN_FLIGHT);
    if (!assets)
        return std::unexpected{ makeError(EngineErrorCode::AssetsFailed, assets.error()) };
    m_assets.emplace(std::move(*assets));
    return {};
}

auto Engine::createFrameGraph() -> std::expected<void, EngineError>
{
    m_instanceBridge.connect(m_world.registry());

    // Headless draws no UI: no window for ImGui's backends, and nothing to look at.
    std::function<void()> buildUi;
    if (m_window)
    {
        ImGui_ImplGlfw_InitForVulkan(m_window->glfwWindow(), true);
        m_imguiGlfw = true;
        buildUi = [this] {
            ImGui_ImplGlfw_NewFrame();
            m_ui.render();
        };
    }

    m_frameGraph = std::make_unique<aegis::render_graph::FrameGraph>(*m_device, m_renderer->extent());
    aegis::render_graph::addDefaultPasses(*m_frameGraph,
        {
            .gpuDriven = useGpuDrivenRendering(),
            .buildUi = std::move(buildUi),
            .context = m_context.get(),
        });
    if (auto compiled = m_frameGraph->compile(); !compiled)
        return std::unexpected{ makeError(EngineErrorCode::FrameGraphFailed, std::format("Failed to compile the frame graph: {}", describe(compiled.error()))) };
    return {};
}

bool Engine::resize()
{
    if (auto resized = m_renderer->resize(aegis::rhi::Extent2D{ m_window->extent() }); !resized)
    {
        log::fatal("Failed to resize the renderer: {}", aegis::renderer::describe(resized.error()));
        return false;
    }
    m_window->resetResized();
    if (auto resized = m_frameGraph->resized(m_renderer->extent()); !resized)
        log::fatal("Failed to resize the frame graph: {}", describe(resized.error()));
    return true;
}

void Engine::updateCameraAspect()
{
    const auto [width, height] = m_window ? m_window->extent() : std::pair{ m_renderer->extent().x, m_renderer->extent().y };
    auto& registry = m_world.registry();
    const auto mainCamera = m_world.mainCamera();
    if (height == 0 || !mainCamera || !registry.has<Camera>(mainCamera))
        return;
    registry.get<Camera>(mainCamera).aspect = static_cast<float>(width) / static_cast<float>(height);
}

// Reads the offscreen target back (the last frame's slot) and writes it as PNG. The device is idle.
auto Engine::saveScreenshot() -> std::expected<void, std::string>
{
    if (!m_renderer->offscreen() || m_renderer->targetFormat() != rhi::Format::RGBA8_UNORM)
        return std::unexpected{ "screenshots need the headless RGBA8 target" };

    const auto& target = m_renderer->offscreenTarget(m_lastFrameIndex);
    const auto extent = m_renderer->extent();
    constexpr std::size_t texelSize{ 4 };
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
    cmd->transitionImageLayout(target.view, rhi::ResourceState::ColorAttachment, rhi::ResourceState::CopySrc);
    cmd->copyImageToBuffer(target.image, m_device->get(*readback), regions);
    cmd->end();
    auto submitted = m_device->graphicsQueue().submit(*cmd);
    if (!submitted)
        return std::unexpected{ "failed to submit the readback" };
    m_device->graphicsQueue().wait(*submitted);

    std::vector<std::byte> pixels(byteSize);
    m_device->get(*readback).read(pixels.data(), byteSize);
    m_device->free(*readback);

    if (!stbi_write_png(m_desc.screenshot.c_str(), static_cast<int>(extent.x), static_cast<int>(extent.y), 4,
            pixels.data(), static_cast<int>(extent.x * texelSize)))
        return std::unexpected{ std::format("failed to write '{}'", m_desc.screenshot) };
    log::info("Screenshot saved: {}", m_desc.screenshot);
    return {};
}

void Engine::applyFrameBrake(std::chrono::steady_clock::time_point frameBegin)
{
    using namespace std::chrono;

    ScopeProfiler frameBrake("Wait for FPS limit");

    if constexpr (!core::ENABLE_FPS_LIMIT)
        return;

    // Note: This has to be a busy wait since sleep_for is not accurate enough (especially on Windows)
    while (duration<double, std::milli>(steady_clock::now() - frameBegin).count() < core::TARGET_FRAME_TIME)
    {
        // busy wait
    }
}
}
