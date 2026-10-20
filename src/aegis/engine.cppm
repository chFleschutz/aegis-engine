export module aegis.engine;

import std;
export import aegis.math;
export import aegis.ui;
export import aegis.editor;
export import aegis.defaults;
export import aegis.scene_description;
export import aegis.core.globals;
export import aegis.core.layer_stack;
export import aegis.paths;
export import aegis.base;
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

    Engine();
    Engine(const Engine&) = delete;
    Engine(Engine&&) = delete;
    ~Engine();

    auto operator=(const Engine&) -> Engine& = delete;
    auto operator=(Engine&&) -> Engine& = delete;

    /// @brief Returns the instance of the engine
    [[nodiscard]] static auto instance() -> Engine&;

    [[nodiscard]] static auto window() -> aegis::platform::Window&;
    [[nodiscard]] static auto input() -> aegis::platform::Input&;
    [[nodiscard]] static auto device() -> aegis::rhi::Device&;
    [[nodiscard]] static auto renderer() -> aegis::renderer::Renderer&;
    [[nodiscard]] static auto frameGraph() -> aegis::render_graph::FrameGraph&;
    [[nodiscard]] static auto drawBatches() -> aegis::render_graph::DrawBatchRegistry&;
    [[nodiscard]] static auto ui() -> ui::UI&;
    [[nodiscard]] static auto scene() -> scene::Scene&;
    [[nodiscard]] static auto defaults() -> const aegis::assets::DefaultAssets&;

    [[nodiscard]] auto useGpuDrivenRendering() const -> bool;

    // Asset helpers: the new stack's loaders with the engine's device and defaults filled in.
    // Failures are logged and asserted, like the engine's other setup errors.

    /// @brief Loads a .gltf / .glb / .obj into the current scene; returns its root entity.
    static auto loadModel(const std::filesystem::path& path) -> scene::Entity;

    /// @brief An environment (sky box and IBL maps) from a Radiance .hdr.
    static auto loadEnvironment(const std::filesystem::path& hdr) -> aegis::assets::Environment;

    /// @brief A PNG/JPEG/... texture; color data (e.g. albedo) is RGBA8_SRGB.
    static auto loadTexture(const std::filesystem::path& path,
        aegis::rhi::Format format = aegis::rhi::Format::RGBA8_UNORM) -> std::shared_ptr<aegis::assets::Texture>;

    /// @brief A new instance of 'materialTemplate' (by default the PBR template).
    static auto createMaterial(std::shared_ptr<const aegis::assets::MaterialTemplate> materialTemplate = nullptr)
        -> std::shared_ptr<aegis::assets::MaterialInstance>;

    void run();

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
    void createRenderer();
    void loadDefaultAssets();
    void createFrameGraph();
    auto defaultEnvironment() const -> aegis::assets::Environment;
    bool resize();
    void updateCameraAspect();
    void applyFrameBrake(std::chrono::steady_clock::time_point frameBegin);

    inline static Engine* s_instance{ nullptr };

    /// @brief Longest frame time the scene and scripts see.
    static constexpr float MAX_FRAME_DELTA_SEC{ 0.1f };

    // Order matters: members are destroyed bottom up, and everything holding GPU resources must go
    // before the device.
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
