export module aegis.engine;

import std;
export import aegis.math;
import aegis.ui;
import aegis.editor;
export import aegis.asset_loader;
export import aegis.instantiate;
export import aegis.scene_description;
import aegis.core.globals;
import aegis.core.layer_stack;
export import aegis.paths;
export import aegis.base;
export import aegis.scene;
export import aegis.scene.components;
export import aegis.scene.render_components;
export import aegis.scene.system;
export import aegis.defaults;
export import aegis.reflect;
export import aegis.scripting.script;
export import aegis.scripting.movement.kinematic_movement_controller;
import aegis.scripting.script_system;
import aegis.scene.systems.camera_system;
import aegis.scene.systems.transform_system;
export import aegis.assets;
import aegis.platform;
import aegis.render_graph;
import aegis.renderer;
import aegis.rhi;

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

    [[nodiscard]] auto useGpuDrivenRendering() const -> bool;

    /// @brief The registered components and scripts. Register a game's scripts here before loadScene.
    [[nodiscard]] auto types() -> reflect::TypeRegistry& { return m_types; }

    void run();

    /// @brief Creates a scene from a description
    /// @tparam T Description of the scene (Derived from SceneDescription)
    template<SceneDescriptionDerived T, typename... Args>
    void loadScene(Args&&... args)
    {
        m_scene.reset();
        // The engine's systems, always present. Scripts go first, they write the transforms the
        // TransformSystem derives from, and the camera reads the global transforms.
        m_scene.addSystem<scripting::ScriptSystem>(m_scene.registry(), &m_input);
        m_scene.addSystem<scene::TransformSystem>();
        m_scene.addSystem<scene::CameraSystem>();
        T description{ std::forward<Args>(args)... };
        SceneContext context{ .scene = m_scene, .types = m_types, .assets = *m_assets };
        description.initialize(context);
        m_scene.begin();
        m_frameGraph->sceneInitialized(m_scene);
    }

private:
    void createRenderer();
    void createAssets();
    void createFrameGraph();
    bool resize();
    static auto createWindow() -> aegis::platform::Window;
    void updateCameraAspect();
    void applyFrameBrake(std::chrono::steady_clock::time_point frameBegin);

    /// @brief Longest frame time the scene and scripts see.
    static constexpr float MAX_FRAME_DELTA_SEC{ 0.1f };

    // Order matters: members are destroyed bottom up, and everything holding GPU resources must go
    // before the device.
    core::LayerStack m_layerStack{};
    ui::UI m_ui{ m_layerStack };
    aegis::platform::Window m_window{ createWindow() };
    // Before ImGui's GLFW backend installs its callbacks, which chain to the window's.
    aegis::platform::Input m_input{ m_window };
    std::unique_ptr<aegis::rhi::Context> m_context;
    std::unique_ptr<aegis::rhi::Device> m_device;
    std::unique_ptr<aegis::renderer::Renderer> m_renderer;
    std::optional<AssetLoader> m_assets;
    reflect::TypeRegistry m_types;
    scene::Scene m_scene;
    aegis::render_graph::DrawBatchRegistry m_drawBatches; // Disconnects from the scene first
    std::unique_ptr<aegis::render_graph::FrameGraph> m_frameGraph;
};
}
