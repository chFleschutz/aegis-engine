module;

#include "core/assert.h"

#include <cstdlib>

#include <imgui.h>
#include <imgui_impl_glfw.h>

module aegis.engine;

import std;

namespace aegis
{
namespace
{
// Setup failures log their cause and exit; the engine can't carry on without a device.
[[noreturn]] void fatalSetup(const std::string& message)
{
    log::fatal("{}", message);
    std::exit(EXIT_FAILURE);
}
}

Engine::Engine()
{
    createRenderer();
    createAssets();
    createFrameGraph();
    m_layerStack.push<editor::EditorLayer>(m_input, *m_renderer, *m_frameGraph, m_drawBatches, m_scene);

    log::info("Engine Initialized!");
    log::info("Using {} Driven Rendering", useGpuDrivenRendering() ? "GPU" : "CPU");
    log::info("\n\n\n"
              "\t\t\t\t      ###   #######   ######   ####   ###### \n"
              "\t\t\t\t     ## ##  ##       ##    ##   ##   ##    ##\n"
              "\t\t\t\t    ##  ##  ##       ##         ##   ##      \n"
              "\t\t\t\t   ##   ##  ######   ##         ##    ###### \n"
              "\t\t\t\t  ########  ##       ##  ####   ##         ##\n"
              "\t\t\t\t ##     ##  ##       ##    ##   ##   ##    ##\n"
              "\t\t\t\t##      ##  #######   ######   ####   ###### \n"
              "\n\n");
}

Engine::~Engine()
{
    // The GPU may still use what the members below free; ImGui's backends go before its context.
    m_device->waitIdle();
    m_frameGraph.reset();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

auto Engine::useGpuDrivenRendering() const -> bool
{
    return ENABLE_GPU_DRIVEN_RENDERING && m_device->capabilities().meshShaders;
}

void Engine::run()
{
    auto lastFrameBegin = std::chrono::steady_clock::now();
    while (!m_window.shouldClose())
    {
        ScopeProfiler frameTime("Frame Time");

        auto currentFrameBegin = std::chrono::steady_clock::now();
        float frameTimeSec = std::chrono::duration<float, std::chrono::seconds::period>(currentFrameBegin - lastFrameBegin).count();
        lastFrameBegin = currentFrameBegin;
        // After a long frame (a breakpoint, a load), don't hand scripts and systems the whole stall
        frameTimeSec = std::min(frameTimeSec, MAX_FRAME_DELTA_SEC);

        m_window.update();
        if (m_window.isMinimized())
        {
            m_window.waitEvents();
            lastFrameBegin = std::chrono::steady_clock::now(); // Not part of the next frame's delta
            continue;
        }
        if (m_window.wasResized() || m_renderer->needsResize())
        {
            if (!resize())
                break;
        }

        // Update: scripts write the transforms, the systems derive from them (camera matrices
        // included), then the editor reads the result.
        // ImGui's flags are from the previous frame's UI; they gate the game input.
        const auto& io = ImGui::GetIO();
        m_input.setCaptured(io.WantCaptureKeyboard, io.WantCaptureMouse);
        updateCameraAspect();
        m_scriptManager.update(frameTimeSec);
        m_scene.update(frameTimeSec);
        m_layerStack.update(frameTimeSec);

        // Rendering: uploads queued this frame (or while loading) go first, on the same queue.
        if (auto flushed = m_device->flushUploads(); !flushed)
        {
            log::fatal("Failed to flush uploads: {}", rhi::describe(flushed.error()));
            break;
        }
        auto rendered = m_renderer->renderFrame([this](const aegis::renderer::Renderer::FrameInfo& frame) {
            m_frameGraph->execute({
                .cmd = frame.cmd,
                .scene = m_scene,
                .drawBatches = m_drawBatches,
                .frameIndex = frame.frameIndex,
                .extent = frame.extent,
                .target = frame.target,
            });
        });
        if (!rendered)
        {
            log::fatal("Failed to render a frame: {}", aegis::renderer::describe(rendered.error()));
            break;
        }

        applyFrameBrake(currentFrameBegin);
    }

    m_device->waitIdle();
}

void Engine::createRenderer()
{
    auto context = aegis::rhi::Context::create({ .appName = "Aegis", .window = &m_window });
    if (!context)
        fatalSetup(std::format("Failed to create the Vulkan context ({})", aegis::rhi::describe(context.error())));
    m_context = std::move(*context);

    auto device = m_context->createDevice({});
    if (!device)
        fatalSetup(std::format("Failed to create the Vulkan device ({})", aegis::rhi::describe(device.error())));
    m_device = std::move(*device);

    auto renderer = aegis::renderer::Renderer::create({
        .context = *m_context,
        .device = *m_device,
        .window = &m_window,
    });
    if (!renderer)
        fatalSetup(std::format("Failed to create the renderer: {}", aegis::renderer::describe(renderer.error())));
    m_renderer = std::move(*renderer);
}

void Engine::createAssets()
{
    auto assets = AssetLoader::create(*m_device, FRAMES_IN_FLIGHT);
    if (!assets)
        fatalSetup(assets.error());
    m_assets.emplace(std::move(*assets));
}

void Engine::createFrameGraph()
{
    m_drawBatches.connect(m_scene.registry());

    ImGui_ImplGlfw_InitForVulkan(m_window.glfwWindow(), true);

    m_frameGraph = std::make_unique<aegis::render_graph::FrameGraph>(*m_device, m_renderer->extent());
    aegis::render_graph::addDefaultPasses(*m_frameGraph,
        {
            .gpuDriven = useGpuDrivenRendering(),
            .drawBatches = m_drawBatches,
            .buildUi = [this] {
                ImGui_ImplGlfw_NewFrame();
                m_ui.render();
            },
            .context = m_context.get(),
        });
    if (auto compiled = m_frameGraph->compile(); !compiled)
    {
        log::fatal("Failed to compile the frame graph: {}", describe(compiled.error()));
        AGX_ASSERT_X(false, "Failed to compile the frame graph");
    }
}

bool Engine::resize()
{
    if (auto resized = m_renderer->resize(aegis::rhi::Extent2D{ m_window.extent() }); !resized)
    {
        log::fatal("Failed to resize the renderer: {}", aegis::renderer::describe(resized.error()));
        return false;
    }
    m_window.resetResized();
    if (auto resized = m_frameGraph->resized(m_renderer->extent()); !resized)
        log::fatal("Failed to resize the frame graph: {}", describe(resized.error()));
    return true;
}

void Engine::updateCameraAspect()
{
    const auto [width, height] = m_window.extent();
    auto& registry = m_scene.registry();
    const auto mainCamera = m_scene.mainCamera();
    if (height == 0 || !mainCamera || !registry.has<Camera>(mainCamera))
        return;
    registry.get<Camera>(mainCamera).aspect = static_cast<float>(width) / static_cast<float>(height);
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
