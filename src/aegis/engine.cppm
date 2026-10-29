export module aegis.engine;

import std;
export import aegis.math;
export import aegis.asset_library;
export import aegis.scene_description;
export import aegis.paths;
export import aegis.core;
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
import aegis.serialize;
import aegis.world.systems.camera_system;
import aegis.world.systems.transform_system;
export import aegis.assets;
import aegis.platform;
import aegis.render_bridge;
import aegis.render_graph;
import aegis.renderer;
import aegis.rhi;

export namespace aegis
{
/// @brief The ImGui context and its platform backend, and the UI's place in the frame: beginFrame opens it
///        after the input was read, endFrame closes it (ImGui::Render) before the frame graph's UI pass
///        records it. ImGui's Vulkan backend belongs to that pass (render_graph::UiPass); the textures the UI
///        shows are registered with it (uiTexture).
class Presentation
{
public:
    Presentation();
    Presentation(const Presentation&) = delete;
    Presentation(Presentation&&) = delete;
    ~Presentation();

    auto operator=(const Presentation&) -> Presentation& = delete;
    auto operator=(Presentation&&) -> Presentation& = delete;

    /// @brief Follows the window's input through ImGui's GLFW backend; without it (headless) the UI is
    ///        drawn, not interacted with.
    void attach(platform::Window& window);
    /// @brief Shuts the GLFW backend down, for attach to start it again. Shutting ImGui's Vulkan backend down (the
    ///        UI pass, when the frame graph is rebuilt) also destroys ImGui's platform windows, the main one too.
    void detach();
    /// @brief Opens the frame. Headless, the display is 'extent' and time advances by 'deltaSeconds'.
    void beginFrame(rhi::Extent2D extent, float deltaSeconds);
    /// @brief Ends the frame: its draw data is ready for the UI pass.
    void endFrame();

    /// @brief An ImTextureID showing 'view' (an image of the frame graph, say) with ImGui::Image, valid in this
    ///        frame only: ask again in every frame that shows it. 0 before the frame graph's UI pass exists.
    /// @note The UI pass samples it: the image must be in a shader read state by then, as a view's "Final" is.
    [[nodiscard]] auto uiTexture(rhi::ImageViewHandle view) -> std::uint64_t;
    /// @brief Creates what uiTexture registers with, once the device exists.
    [[nodiscard]] auto createTextures(rhi::Device& device) -> std::expected<void, std::string>;
    /// @brief Releases every texture uiTexture handed out. The GPU must be idle: call it before the frame graph is
    ///        resized (its views are recreated) or rebuilt (the UI pass goes, and ImGui's backend with it).
    void releaseTextures();
    /// @brief Releases the textures and what they are registered with, while the device and the UI pass exist.
    void destroyTextures();

private:
    struct Textures; // render_graph::UiTextures, kept out of this interface

    bool m_glfw{ false }; // ImGui's GLFW backend is initialized
    std::unique_ptr<Textures> m_textures;
};

/// @brief How the app starts: a window or an offscreen target, and when it stops.
struct AppDesc
{
    std::string title{ "Aegis" };
    /// @brief Window size, or the size of the offscreen target when headless.
    std::uint32_t width{ 1920 };
    std::uint32_t height{ 1080 };
    /// @brief No window, input or UI: frames render to an offscreen target and the run ends by itself.
    bool headless{ false };
    /// @brief Frames to render before run() returns; 0 runs until the window is closed (headless: 600).
    std::uint32_t frames{ 0 };
    /// @brief Headless: save the last frame here as PNG.
    std::string screenshot;
    /// @brief With a shell's view: the screenshot shows the view's target only, not the window's whole frame
    ///        with the shell's UI over it.
    bool screenshotView{ false };
    /// @brief GPU-driven rendering (mesh shaders) where the device supports it; false renders CPU-driven.
    bool gpuDriven{ true };
    /// @brief The project folder the assets come from, its index kept in `<project>/.aegis`; empty for
    ///        the samples (paths::project()). A game's launcher fills it in through paths::locate.
    std::filesystem::path project;
    /// @brief The scene file the launcher starts with, by its path in the project's content/ folder ("scenes/sponza.scene");
    ///        empty for the game's own (GameModule::startupScene).
    std::string scene;
    /// @brief An asset ID: print the import cache entry of that model, mesh of a model, texture or environment
    ///        instead of running (see dumpAsset).
    std::string dumpAsset;
    /// @brief "all" opens every panel of the editor at startup (a test, a screenshot); empty keeps the editor's own
    ///        layout. The runtime flavor of a game ignores it.
    std::string editorPanels;
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

/// @brief Reads --headless, --frames <n>, --screenshot <png>, --width <n>, --height <n>, --project <dir>,
///        --scene <path>, --dump-asset <id> and --editor-panels all;
///        the first argument (the program name) is skipped. A headless run without --frames stops after 600 frames.
[[nodiscard]] auto parseArgs(std::span<const char* const> args) -> std::expected<AppDesc, EngineError>;

/// @brief The same for main's argc and argv.
[[nodiscard]] auto parseArgs(int argc, char** argv) -> std::expected<AppDesc, EngineError>;

/// @brief The usage line for parseArgs's options, naming 'program'.
[[nodiscard]] auto usage(std::string_view program) -> std::string;

/// @brief The import cache entry of the asset AppDesc::dumpAsset names, as text (assets::ImportCache::dump and
///        dumpTexture): a model file's entry, one mesh blob of it for the ID of a model's mesh, or the texture entry
///        of a texture or environment file or of a model's embedded texture. Reads the project's asset index; needs
///        no device.
/// @return EngineErrorCode::InvalidArguments for a malformed ID, EngineErrorCode::AssetsFailed for an unknown ID, an
///         asset that is not cached (only models, their meshes and textures, and environments are) or one without an
///         entry yet.
[[nodiscard]] auto dumpAsset(const AppDesc& desc) -> std::expected<std::string, EngineError>;

class Engine;

/// @brief A game as the launcher sees it. AEGIS_GAME_MODULE (`aegis/launch/game_module.h`) defines the one
///        function that returns it; the launcher (`<Game>` and `<Game>-Editor`, see aegis_add_game) calls it.
struct GameModule
{
    /// @brief The game's name, also the window title.
    std::string_view name;
    /// @brief The scene file the launcher loads after onStartup, by its path in the project; empty for none.
    ///        --scene replaces it.
    std::string_view startupScene;
    /// @brief Registers the game's components and scripts in the engine's types; null for none.
    auto (*registerTypes)(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>{ nullptr };
    /// @brief Adds the game's systems to each world, after the engine's (Engine::setWorldSetup); null for none.
    auto (*setupWorld)(Engine& engine, world::World& world) -> void{ nullptr };
    /// @brief Runs once the engine is created and the types are registered, e.g. to load the first scene;
    ///        null for none.
    auto (*onStartup)(Engine& engine) -> std::expected<void, EngineError>{ nullptr };
};

/// @brief Which input a shell keeps from the game this frame (its UI has it).
struct InputCapture
{
    bool keyboard{ false };
    bool mouse{ false };
};

/// @brief What a shell renders the world through instead of the window: a camera, and a target of its own size.
struct RenderView
{
    using Camera = render_graph::FrameScene::Camera;

    /// @brief Replaces the world's main camera this frame; none renders through the main camera.
    std::optional<Camera> camera;
    /// @brief The target's size, apart from the window's; at least 1x1. The scene renders into the frame graph's
    ///        "Final" at this size, and the window shows only the UI.
    rhi::Extent2D extent;
};

/// @brief What a shell gets at the frame's safe point: input has been read and the ImGui frame is open,
///        and no world is being updated or rendered, so the shell can read and write the worlds directly. Here it
///        also decides which worlds update this frame (update) and which one renders (render).
struct FrameContext
{
    /// @brief The engine, which creates and destroys worlds (Engine::createWorld).
    Engine& engine;
    /// @brief The engine's main world (Engine::world), which scenes load into.
    world::World& world;
    /// @brief The game's and the engine's registered components and scripts (Engine::types)
    const reflect::TypeRegistry& types;
    /// @brief Loads the assets the UI assigns (AssetLibrary::resolve) and names them through its database
    AssetLibrary& assets;
    /// @brief Read-only introspection: the passes, their resources and timings. It stays as it is until the
    ///        frame has rendered, so its textures can be shown in the UI (Presentation::uiTexture).
    render_graph::FrameGraph& frameGraph;
    /// @brief The extent of the view the frame graph renders into this frame (the shell's last RenderView); none
    ///        when it renders onto the window. Only then is its "Final" in a state the UI can show.
    std::optional<rhi::Extent2D> viewExtent;
    Presentation& presentation;
    /// @brief The main world's instances.
    const render_graph::InstanceStore& instances;
    const renderer::Renderer& renderer;
    /// @brief The time since the last frame, which the UI runs by; each world updates by its own (World::clock).
    float deltaSeconds;

    /// @brief Updates 'world', one of the engine's, in 'mode' this frame, by its own clock, once onFrame has returned
    ///        (so this frame's edits show). A world begins with its first update. A world the shell doesn't update
    ///        stays as it is, and its clock stands still. A second call for the same world replaces the mode.
    void update(world::World& world, world::UpdateMode mode);
    /// @brief Renders 'world', one of the engine's, this frame instead of the main world, as its last update left it.
    void render(world::World& world);
};

/// @brief Decides, each frame, what the app shows around the game: its UI, which worlds update and render and
///        who gets the input. The launcher picks it: the runtime installs GameShell (the default), the editor
///        flavor installs editor::EditorShell. Install it before run() (Engine::setShell).
class Shell
{
public:
    Shell() = default;
    Shell(const Shell&) = delete;
    Shell(Shell&&) = delete;
    virtual ~Shell() = default;

    auto operator=(const Shell&) -> Shell& = delete;
    auto operator=(Shell&&) -> Shell& = delete;

    /// @brief Runs once Engine::run starts: after the project, the game's types and its startup scene have loaded.
    virtual auto onStartup(Engine& engine) -> std::expected<void, EngineError> { return {}; }
    /// @brief The safe point, once per frame: build the UI, apply edits and update the worlds that should: Play runs
    ///        the game, Edit only the systems that opt in (see FrameContext).
    virtual void onFrame(FrameContext& frame) = 0;
    /// @brief Asked when the user closes the window: false keeps it open, e.g. to ask about unsaved changes first and
    ///        call Engine::quit once answered.
    [[nodiscard]] virtual auto canClose() -> bool { return true; }
    /// @brief The input the game doesn't see this frame; asked before the input is read, and again after onFrame,
    ///        before the worlds update, so a shortcut the UI ran this frame keeps its key from the game.
    [[nodiscard]] virtual auto inputCapture() const -> InputCapture { return {}; }
    /// @brief The view to render; none renders onto the window (or, headless, the offscreen target). Asked before
    ///        onFrame for the frame graph, which is rebuilt when a view appears or goes and resized when its extent
    ///        changes, and after it for the camera: a change of the extent the UI makes applies from the next frame,
    ///        so the graph's textures the UI shows stay valid until they are drawn. Keep the extent steady while a
    ///        panel is being dragged.
    [[nodiscard]] virtual auto renderView() const -> std::optional<RenderView> { return std::nullopt; }
    /// @brief Runs before the engine tears down, while the world, the frame graph and ImGui still exist.
    virtual void onShutdown() {}
};

/// @brief The runtime: the game plays in the main world and gets all the input, with no UI around it.
class GameShell final : public Shell
{
public:
    void onFrame(FrameContext& frame) override { frame.update(frame.world, world::UpdateMode::Play); }
};

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

    /// @brief Finds assets by path and loads them by ID.
    [[nodiscard]] auto assets() -> AssetLibrary& { return *m_assets; }

    /// @brief The main world: scenes load into it (loadScene), and it renders unless the shell renders another
    ///        (FrameContext::render). It exists from create() on and is never destroyed.
    [[nodiscard]] auto world() -> world::World& { return *m_mainWorld; }

    /// @brief A new, empty world beside the main one (a play-in-editor copy, a menu beside a level), with the engine's
    ///        systems and the game's (setWorldSetup), a clock of its own (a fixed step when headless) and instances
    ///        of its own. Fill it before run() or at the safe point (Shell::onFrame); it begins with its first
    ///        update (FrameContext::update).
    [[nodiscard]] auto createWorld() -> world::World&;

    /// @brief Destroys a world createWorld made, with its entities and instances, before run() or at the safe point.
    ///        This frame's update and render of it are dropped: the main world renders instead.
    void destroyWorld(world::World& world);

    /// @brief Adds the game's systems to every world, after the engine's (scripts, transforms, cameras): to the worlds
    ///        there are and to each new one, and again to each one a scene loads into. Set it once, before the first
    ///        scene loads; the launcher sets GameModule::setupWorld.
    void setWorldSetup(std::function<void(world::World& world)> setup);

    /// @brief Replaces the main world's entities with a scene from a description. It begins with its next update.
    /// @tparam T Description of the scene (Derived from SceneDescription)
    template<SceneDescriptionDerived T, typename... Args>
    void loadScene(Args&&... args)
    {
        newScene();
        T description{ std::forward<Args>(args)... };
        SceneContext context{ .scene = world(), .types = m_types, .assets = *m_assets };
        description.initialize(context);
    }

    /// @brief Replaces the main world's entities with the scene file 'scene': its entities with the IDs, components
    ///        and scripts the file gives them, and the assets they reference. Register the game's types first
    ///        (types()). The world begins with its next update.
    /// @return EngineErrorCode::SceneFailed, naming the file and line, if the file does not load; the world is
    ///         then empty.
    [[nodiscard]] auto loadScene(assets::AssetId scene) -> std::expected<void, EngineError>;

    /// @brief The scene file the main world was loaded from last (loadScene); none before, after a failed load and
    ///        after newScene.
    [[nodiscard]] auto scene() const -> std::optional<assets::AssetId> { return m_scene; }

    /// @brief Empties the main world for a new scene that has no file yet. It begins with its next update.
    void newScene();

    /// @brief Sets the window's title; headless, nothing.
    void setTitle(std::string_view title);

    /// @brief Shows the cursor over the window again (platform::CursorMode::Normal) after a game hid or disabled it;
    ///        headless, nothing.
    void restoreCursor();

    /// @brief How much larger than 100% the window's monitor shows things (high DPI), as of now; 1 headless.
    [[nodiscard]] auto contentScale() const -> float;

    /// @brief Ends run() after this frame, without asking the shell (Shell::canClose).
    void quit() { m_quit = true; }

    /// @brief Replaces the shell (GameShell by default), such as with the editor (editor::EditorShell). Call it
    ///        before run().
    void setShell(std::unique_ptr<Shell> shell);

    /// @brief The project folder (AppDesc::project); empty for the samples.
    [[nodiscard]] auto project() const -> const std::filesystem::path& { return m_desc.project; }
    /// @brief How the app was started.
    [[nodiscard]] auto desc() const -> const AppDesc& { return m_desc; }

    /// @brief No window, input or platform backend: frames render offscreen (AppDesc::headless).
    [[nodiscard]] auto headless() const -> bool { return !m_window; }

private:
    friend struct FrameContext; // Its update and render ask for this frame's updates and rendered world

    // FrameContext::update: a world the shell updates this frame, and how.
    struct WorldUpdate
    {
        world::World* world;
        world::UpdateMode mode;
    };

    explicit Engine(const AppDesc& desc);

    auto initialize() -> std::expected<void, EngineError>;
    auto createWindow() -> std::expected<void, EngineError>;
    auto createRenderer() -> std::expected<void, EngineError>;
    auto createAssets() -> std::expected<void, EngineError>;
    /// @brief Replaces the frame graph with one rendering onto the renderer's target, or into a view of 'view's extent.
    ///        Frames in flight must have finished.
    auto createFrameGraph(std::optional<rhi::Extent2D> view = std::nullopt) -> std::expected<void, EngineError>;
    bool resize();
    /// @brief Rebuilds or resizes the frame graph for the shell's view, if it changed since the last frame.
    bool followView(const std::optional<RenderView>& view);
    /// @brief Gives every world's main camera the aspect of what the frame graph renders into.
    void updateCameraAspect(rhi::Extent2D extent);
    /// @brief Opens the ImGui frame, lets the shell build its UI, apply its edits and pick the worlds that update and
    ///        render, and ends the frame.
    void runShell(float deltaSeconds);
    /// @brief Keeps from the game what the shell's inputCapture asks for. Not headless.
    void captureInput();
    /// @brief Updates the worlds the shell asked for, each by its own clock, ticked at 'now'; the others' clocks stand still.
    void updateWorlds(core::FrameClock::Clock::time_point now);
    /// @brief Restarts every clock at 'now', so the time until then (a minimized window) is no part of the next delta.
    void resetClocks(core::FrameClock::Clock::time_point now);
    /// @brief Adds the engine's systems and the game's (setWorldSetup) to a new or reset world.
    void setUpWorld(world::World& world);
    /// @brief Empties the main world and sets it up again, ready for a scene's entities.
    void resetWorld();
    auto saveScreenshot() -> std::expected<void, std::string>;

    /// @brief Longest frame time the scene and scripts see.
    static constexpr float MAX_FRAME_DELTA_SEC{ 0.1f };

    // Order matters: members are destroyed bottom up, and everything holding GPU resources must go
    // before the device.
    AppDesc m_desc;
    std::unique_ptr<aegis::platform::Window> m_window; // null when headless
    // Before ImGui's GLFW backend installs its callbacks, which chain to the window's. Null when headless.
    std::unique_ptr<aegis::platform::Input> m_input;
    Presentation m_presentation;         // The ImGui context: outlives the frame graph's UI pass and the shell
    std::uint32_t m_lastFrameIndex{ 0 }; // The frame slot the last frame drew into
    std::unique_ptr<aegis::rhi::Context> m_context;
    std::unique_ptr<aegis::rhi::Device> m_device;
    std::unique_ptr<aegis::renderer::Renderer> m_renderer;
    std::optional<AssetLibrary> m_assets;
    reflect::TypeRegistry m_types;
    core::FrameClock m_clock{ core::FrameClock::clamped(MAX_FRAME_DELTA_SEC) }; // The UI's; each world has its own
    std::function<void(world::World&)> m_worldSetup;                            // The game's systems
    // Their instances hold meshes and materials: they go before the device.
    Worlds m_worlds{ [this](world::World& world) { setUpWorld(world); } };
    world::World* m_mainWorld{ nullptr };          // The first of m_worlds
    std::vector<WorldUpdate> m_updates;            // This frame's, from the shell (FrameContext::update)
    world::World* m_renderRequest{ nullptr };      // This frame's, from the shell (FrameContext::render); none for the main world
    const world::World* m_lastRendered{ nullptr }; // The world whose instances the passes' buffers hold
    std::unique_ptr<aegis::render_graph::FrameGraph> m_frameGraph;
    std::optional<rhi::Extent2D> m_viewExtent; // The extent of the view the frame graph renders into; none for the target
    std::optional<assets::AssetId> m_scene;    // The scene file the main world was loaded from
    bool m_quit{ false };                      // quit(): run() ends after this frame
    // Last, so it goes first, while everything it may reference (the worlds, the frame graph, ImGui) exists.
    std::unique_ptr<Shell> m_shell{ std::make_unique<GameShell>() };
};
}
