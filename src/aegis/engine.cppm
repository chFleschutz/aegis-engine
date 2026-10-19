module;

#include "core/assert.h"

#include <aegis-log/log.h>

#include <cstdlib>

#include <imgui.h>
#include <imgui_impl_glfw.h>

export module aegis.engine;

import std;
export import aegis.math;
export import aegis.ui;
export import aegis.editor;
export import aegis.defaults;
export import aegis.scene_description;
export import aegis.core.globals;
export import aegis.core.layer_stack;
export import aegis.core.logging;
export import aegis.core.profiler;
export import aegis.scene;
export import aegis.scene.components;
export import aegis.scene.system;
export import aegis.scripting.script_base;
export import aegis.scripting.script_manager;
export import aegis.assets;
export import aegis.platform;
export import aegis.render_graph;
export import aegis.renderer;
export import aegis.rhi;

export namespace aegis
{
class Engine
{
public:
    /// @brief GPU-driven rendering (mesh shaders) where the device supports it, CPU-driven otherwise.
    static constexpr bool ENABLE_GPU_DRIVEN_RENDERING{ true };
    static constexpr std::uint32_t FRAMES_IN_FLIGHT{ aegis::renderer::Renderer::maxFramesInFlight };

    Engine()
    {
        AGX_ASSERT_X(!s_instance, "Only one instance of Engine is allowed");
        s_instance = this;

        createRenderer();
        loadDefaultAssets();
        createFrameGraph();
        m_layerStack.push<editor::EditorLayer>(m_input, *m_renderer, *m_frameGraph, m_drawBatches, m_scene);

        ALOG::info("Engine Initialized!");
        ALOG::info("Using {} Driven Rendering", useGpuDrivenRendering() ? "GPU" : "CPU");
        Logging::logo();
    }

    Engine(const Engine&) = delete;
    Engine(Engine&&) = delete;
    ~Engine()
    {
        // The GPU may still use what the members below free; ImGui's backends go before its context.
        m_device->waitIdle();
        m_frameGraph.reset();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        s_instance = nullptr;
    }

    auto operator=(const Engine&) -> Engine& = delete;
    auto operator=(Engine&&) -> Engine& = delete;

    /// @brief Returns the instance of the engine
    [[nodiscard]] static auto instance() -> Engine&
    {
        AGX_ASSERT_X(s_instance, "Engine instance not created yet");
        return *s_instance;
    }

    [[nodiscard]] static auto window() -> aegis::platform::Window& { return instance().m_window; }
    [[nodiscard]] static auto input() -> aegis::platform::Input& { return instance().m_input; }
    [[nodiscard]] static auto device() -> aegis::rhi::Device& { return *instance().m_device; }
    [[nodiscard]] static auto renderer() -> aegis::renderer::Renderer& { return *instance().m_renderer; }
    [[nodiscard]] static auto frameGraph() -> aegis::render_graph::FrameGraph& { return *instance().m_frameGraph; }
    [[nodiscard]] static auto drawBatches() -> aegis::render_graph::DrawBatchRegistry& { return instance().m_drawBatches; }
    [[nodiscard]] static auto ui() -> ui::UI& { return instance().m_ui; }
    [[nodiscard]] static auto scene() -> scene::Scene& { return instance().m_scene; }
    [[nodiscard]] static auto defaults() -> const aegis::assets::DefaultAssets& { return *instance().m_defaults; }

    [[nodiscard]] auto useGpuDrivenRendering() const -> bool
    {
        return ENABLE_GPU_DRIVEN_RENDERING && m_device->capabilities().meshShaders;
    }

    // Asset helpers: the new stack's loaders with the engine's device and defaults filled in.
    // Failures are logged and asserted, like the engine's other setup errors.

    /// @brief Loads a .gltf / .glb / .obj into the current scene; returns its root entity.
    static auto loadModel(const std::filesystem::path& path) -> scene::Entity
    {
        auto& engine = instance();
        auto root = aegis::assets::Loader::load(
            {
                .device = *engine.m_device,
                .scene = engine.m_scene.registry(),
                .pbrTemplate = engine.m_defaults->pbrTemplate,
                .defaultMaterial = engine.m_defaults->pbrMaterial,
                .framesInFlight = FRAMES_IN_FLIGHT,
            },
            path);
        return check(std::move(root), std::format("load model '{}'", path.string()));
    }

    /// @brief An environment (sky box and IBL maps) from a Radiance .hdr.
    static auto loadEnvironment(const std::filesystem::path& hdr) -> aegis::assets::Environment
    {
        return check(instance().m_ibl->loadEnvironment(hdr), std::format("load environment '{}'", hdr.string()));
    }

    /// @brief A PNG/JPEG/... texture; color data (e.g. albedo) is RGBA8_SRGB.
    static auto loadTexture(const std::filesystem::path& path,
        aegis::rhi::Format format = aegis::rhi::Format::RGBA8_UNORM) -> std::shared_ptr<aegis::assets::Texture>
    {
        return check(aegis::assets::Texture::loadFromFile(*instance().m_device, path, format),
            std::format("load texture '{}'", path.string()));
    }

    /// @brief A new instance of 'materialTemplate' (by default the PBR template).
    static auto createMaterial(std::shared_ptr<const aegis::assets::MaterialTemplate> materialTemplate = nullptr)
        -> std::shared_ptr<aegis::assets::MaterialInstance>
    {
        auto& engine = instance();
        if (!materialTemplate)
            materialTemplate = engine.m_defaults->pbrTemplate;
        return check(aegis::assets::MaterialInstance::create(*engine.m_device, std::move(materialTemplate), FRAMES_IN_FLIGHT),
            "create a material");
    }

    void run()
    {
        auto lastFrameBegin = std::chrono::steady_clock::now();
        while (!m_window.shouldClose())
        {
            ScopeProfiler frameTime("Frame Time");

            auto currentFrameBegin = std::chrono::steady_clock::now();
            float frameTimeSec = std::chrono::duration<float, std::chrono::seconds::period>(currentFrameBegin - lastFrameBegin).count();
            lastFrameBegin = currentFrameBegin;

            m_window.update();
            if (m_window.isMinimized())
            {
                m_window.waitEvents();
                continue;
            }
            if (m_window.wasResized() || m_renderer->needsResize())
            {
                if (!resize())
                    break;
            }

            // Update
            m_scene.update(frameTimeSec);
            m_scriptManager.update(frameTimeSec);
            m_layerStack.update(frameTimeSec);

            // Rendering: uploads queued this frame (or while loading) go first, on the same queue.
            std::ignore = m_device->flushUploads();
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
                ALOG::fatal("Failed to render a frame (renderer error {})", std::to_underlying(rendered.error()));
                break;
            }

            applyFrameBrake(currentFrameBegin);
        }

        m_device->waitIdle();
    }

    /// @brief Creates a scene from a description
    /// @tparam T Description of the scene (Derived from SceneDescription)
    template<SceneDescriptionDerived T, typename... Args>
    void loadScene(Args&&... args)
    {
        m_scene.reset();
        createDefaultScene(m_scene, m_scriptManager, defaultEnvironment());
        T description{ std::forward<Args>(args)... };
        description.initialize(m_scene, m_scriptManager);
        m_scene.begin();
        m_frameGraph->sceneInitialized(m_scene);
    }

private:
    template<typename T, typename E>
    static auto check(std::expected<T, E> result, const std::string& what) -> T
    {
        if (!result)
        {
            fatalSetup(std::format("Failed to {}: {}", what, result.error().message));
        }
        return std::move(*result);
    }

    // Setup failures log their cause and exit; the engine can't carry on without a device.
    [[noreturn]] static void fatalSetup(const std::string& message)
    {
        ALOG::fatal("{}", message);
        std::exit(EXIT_FAILURE);
    }

    void createRenderer()
    {
        auto context = aegis::rhi::Context::create({ .appName = "Aegis", .window = &m_window });
        if (!context)
            fatalSetup(std::format("Failed to create the Vulkan context (rhi error {})", std::to_underlying(context.error().code)));
        m_context = std::move(*context);

        auto device = m_context->createDevice({});
        if (!device)
            fatalSetup(std::format("Failed to create the Vulkan device (rhi error {})", std::to_underlying(device.error().code)));
        m_device = std::move(*device);

        auto renderer = aegis::renderer::Renderer::create({
            .context = *m_context,
            .device = *m_device,
            .window = &m_window,
        });
        if (!renderer)
            fatalSetup(std::format("Failed to create the renderer (renderer error {})", std::to_underlying(renderer.error())));
        m_renderer = std::move(*renderer);
    }

    void loadDefaultAssets()
    {
        m_defaults = check(aegis::assets::DefaultAssets::create(*m_device, FRAMES_IN_FLIGHT), "create the default assets");
        auto ibl = aegis::render_graph::Ibl::create(*m_device);
        if (!ibl)
            fatalSetup(std::format("Failed to create the IBL pipelines: {}", ibl.error().message));
        m_ibl.emplace(std::move(*ibl));
        m_brdfLut = check(m_ibl->brdfLut(), "compute the BRDF lookup table");
    }

    void createFrameGraph()
    {
        m_drawBatches.connect(m_scene.registry());

        ImGui_ImplGlfw_InitForVulkan(m_window.glfwWindow(), true);

        m_frameGraph = std::make_unique<aegis::render_graph::FrameGraph>(*m_device, *m_renderer);
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
            ALOG::fatal("Failed to compile the frame graph: {}", compiled.error().message);
            AGX_ASSERT_X(false, "Failed to compile the frame graph");
        }
    }

    /// @brief A black sky; scenes replace it with Engine::loadEnvironment.
    auto defaultEnvironment() const -> aegis::assets::Environment
    {
        return aegis::assets::Environment{
            .skybox = m_defaults->blackCube,
            .irradiance = m_defaults->blackCube,
            .prefiltered = m_defaults->blackCube,
            .brdfLUT = m_brdfLut,
        };
    }

    /// @return false after a fatal error, which is logged: the loop stops.
    bool resize()
    {
        if (auto resized = m_renderer->resize(aegis::rhi::Extent2D{ m_window.extent() }); !resized)
        {
            ALOG::fatal("Failed to resize the renderer (renderer error {})", std::to_underlying(resized.error()));
            return false;
        }
        m_window.resetResized();
        if (auto resized = m_frameGraph->resized(); !resized)
            ALOG::fatal("Failed to resize the frame graph: {}", resized.error().message);
        return true;
    }

    void applyFrameBrake(std::chrono::steady_clock::time_point frameBegin)
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

    inline static Engine* s_instance{ nullptr };

    // Order matters: members are destroyed bottom up, and everything holding GPU resources must go
    // before the device.
    Logging m_logging{};
    core::LayerStack m_layerStack{};
    ui::UI m_ui{ m_layerStack };
    aegis::platform::Window m_window{ { .title = "Aegis", .width = core::DEFAULT_WIDTH, .height = core::DEFAULT_HEIGHT } };
    // Before ImGui's GLFW backend installs its callbacks, which chain to the window's.
    aegis::platform::Input m_input{ m_window };
    std::unique_ptr<aegis::rhi::Context> m_context;
    std::unique_ptr<aegis::rhi::Device> m_device;
    std::unique_ptr<aegis::renderer::Renderer> m_renderer;
    std::optional<aegis::assets::DefaultAssets> m_defaults;
    std::optional<aegis::render_graph::Ibl> m_ibl;
    std::shared_ptr<aegis::assets::Texture> m_brdfLut;
    scene::Scene m_scene;
    scripting::ScriptManager m_scriptManager{ m_scene, m_input };
    aegis::render_graph::DrawBatchRegistry m_drawBatches; // Disconnects from the scene first
    std::unique_ptr<aegis::render_graph::FrameGraph> m_frameGraph;
};
}
