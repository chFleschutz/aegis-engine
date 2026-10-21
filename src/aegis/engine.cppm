export module aegis.engine;

import std;
export import aegis.math;
import aegis.ui;
import aegis.editor;
export import aegis.asset_library;
export import aegis.instantiate;
export import aegis.scene_description;
import aegis.core.globals;
import aegis.core.layer_stack;
export import aegis.paths;
export import aegis.base;
export import aegis.world;
export import aegis.world.components;
export import aegis.world.render_components;
export import aegis.world.system;
export import aegis.world.types;
export import aegis.defaults;
export import aegis.reflect;
export import aegis.scripting.script;
export import aegis.scripting.movement.kinematic_movement_controller;
import aegis.scripting.script_system;
import aegis.world.systems.camera_system;
import aegis.world.systems.transform_system;
export import aegis.assets;
import aegis.platform;
import aegis.render_graph;
import aegis.renderer;
import aegis.rhi;

export namespace aegis
{
/// @brief How the app starts: a window or an offscreen target, and when it stops.
struct AppDesc
{
    std::string title{ "Aegis" };
    /// @brief Window size, or the size of the offscreen target when headless.
    std::uint32_t width{ core::DEFAULT_WIDTH };
    std::uint32_t height{ core::DEFAULT_HEIGHT };
    /// @brief No window, input or UI: frames render to an offscreen target and the run ends by itself.
    bool headless{ false };
    /// @brief Frames to render before run() returns; 0 runs until the window is closed (headless: 600).
    std::uint32_t frames{ 0 };
    /// @brief Headless: save the last frame here as PNG.
    std::string screenshot;
};

enum class EngineErrorCode
{
    InvalidArguments,
    WindowFailed,
    DeviceFailed,
    RendererFailed,
    AssetsFailed,
    TypesFailed,
    FrameGraphFailed,
    SceneFailed,
};

struct EngineError
{
    EngineErrorCode code;
    std::string message;
};

/// @brief What 'main' returns: run() succeeded, run() or Engine::create failed (an error, a failed
///        screenshot or a validation error), or the command line was wrong. 'aegis-test' uses the same codes.
inline constexpr int exitSuccess{ 0 };
inline constexpr int exitFailure{ 1 };
inline constexpr int exitUsage{ 2 };

[[nodiscard]] auto describe(const EngineError& error) -> std::string;

/// @brief Reads --headless, --frames <n>, --screenshot <png>, --width <n> and --height <n>; the first
///        argument (the program name) is skipped. A headless run without --frames stops after 600 frames.
[[nodiscard]] auto parseArgs(std::span<const char* const> args) -> std::expected<AppDesc, EngineError>;

/// @brief The same for main's argc and argv.
[[nodiscard]] auto parseArgs(int argc, char** argv) -> std::expected<AppDesc, EngineError>;

class Engine;

/// @brief parseArgs plus Engine::create for a 'main': a failure is logged and comes back as the exit code
///        to return (exitUsage for a bad command line, exitFailure when the engine can't start).
[[nodiscard]] auto createEngine(int argc, char** argv) -> std::expected<std::unique_ptr<Engine>, int>;

class Engine
{
public:
    /// @brief GPU-driven rendering (mesh shaders) where the device supports it, CPU-driven otherwise.
    static constexpr bool ENABLE_GPU_DRIVEN_RENDERING{ true };
    static constexpr std::uint32_t FRAMES_IN_FLIGHT{ aegis::renderer::Renderer::maxFramesInFlight };

    /// @brief Creates the window (unless headless), the device, renderer, assets and frame graph.
    [[nodiscard]] static auto create(const AppDesc& desc = {}) -> std::expected<std::unique_ptr<Engine>, EngineError>;

    Engine(const Engine&) = delete;
    Engine(Engine&&) = delete;
    ~Engine();

    auto operator=(const Engine&) -> Engine& = delete;
    auto operator=(Engine&&) -> Engine& = delete;

    [[nodiscard]] auto useGpuDrivenRendering() const -> bool;

    /// @brief The registered components and scripts, the built-in ones included. Register a game's
    ///        components (world::registerComponent) and scripts (scripting::registerScript) here before loadScene.
    [[nodiscard]] auto types() -> reflect::TypeRegistry& { return m_types; }

    /// @brief Runs until the window closes or AppDesc::frames have rendered.
    /// @return An exit code for main: exitSuccess, or exitFailure after a failed frame, a failed screenshot
    ///         or a validation error. The cause is logged.
    [[nodiscard]] auto run() -> int;

    /// @brief Hands out the delta time of each frame. A wall clock with a clamp by default (a fixed
    ///        step when headless); assign FrameClock::fixed(step) for a deterministic run.
    [[nodiscard]] auto clock() -> base::FrameClock& { return m_clock; }

    /// @brief Finds assets by path and loads them by ID.
    [[nodiscard]] auto assets() -> AssetLibrary& { return *m_assets; }

    /// @brief Creates a scene from a description
    /// @tparam T Description of the scene (Derived from SceneDescription)
    template<SceneDescriptionDerived T, typename... Args>
    void loadScene(Args&&... args)
    {
        resetWorld();
        T description{ std::forward<Args>(args)... };
        SceneContext context{ .scene = m_world, .types = m_types, .assets = *m_assets };
        description.initialize(context);
        beginWorld();
    }

    /// @brief Replaces the world with the scene file 'scene': its entities with the IDs, components and scripts
    ///        the file gives them, and the assets they reference. Register the game's types first (types()).
    /// @return EngineErrorCode::SceneFailed, naming the file and line, if the file does not load; the world is
    ///         then empty.
    [[nodiscard]] auto loadScene(assets::AssetId scene) -> std::expected<void, EngineError>;

private:
    explicit Engine(const AppDesc& desc);

    auto initialize() -> std::expected<void, EngineError>;
    auto createWindow() -> std::expected<void, EngineError>;
    auto createRenderer() -> std::expected<void, EngineError>;
    auto createAssets() -> std::expected<void, EngineError>;
    auto createFrameGraph() -> std::expected<void, EngineError>;
    bool resize();
    void updateCameraAspect();
    /// @brief Empties the world and adds the engine's systems, ready for a scene's entities.
    void resetWorld();
    /// @brief Starts the world's systems and hands the world to the frame graph.
    void beginWorld();
    auto saveScreenshot() -> std::expected<void, std::string>;
    void applyFrameBrake(std::chrono::steady_clock::time_point frameBegin);

    /// @brief Longest frame time the scene and scripts see.
    static constexpr float MAX_FRAME_DELTA_SEC{ 0.1f };

    // Order matters: members are destroyed bottom up, and everything holding GPU resources must go
    // before the device.
    core::LayerStack m_layerStack{};
    ui::UI m_ui{ m_layerStack };
    AppDesc m_desc;
    std::unique_ptr<aegis::platform::Window> m_window; // null when headless
    // Before ImGui's GLFW backend installs its callbacks, which chain to the window's. Null when headless.
    std::unique_ptr<aegis::platform::Input> m_input;
    bool m_imguiGlfw{ false };           // ImGui's GLFW backend is initialized
    std::uint32_t m_lastFrameIndex{ 0 }; // The frame slot the last frame drew into
    std::unique_ptr<aegis::rhi::Context> m_context;
    std::unique_ptr<aegis::rhi::Device> m_device;
    std::unique_ptr<aegis::renderer::Renderer> m_renderer;
    std::optional<AssetLibrary> m_assets;
    reflect::TypeRegistry m_types;
    base::FrameClock m_clock{ base::FrameClock::clamped(MAX_FRAME_DELTA_SEC) };
    world::World m_world;
    aegis::render_graph::DrawBatchRegistry m_drawBatches; // Disconnects from the scene first
    std::unique_ptr<aegis::render_graph::FrameGraph> m_frameGraph;
};
}
