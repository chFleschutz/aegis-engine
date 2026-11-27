export module aegis.engine;

import std;
export import aegis.asset_library;
export import aegis.assets;
export import aegis.audio;
export import aegis.reflect;
export import aegis.scene_description;
export import aegis.world;
import aegis.core;
import aegis.serialize;
import aegis.platform;
import aegis.render_bridge;
import aegis.render_graph;
import aegis.render_passes;
import aegis.renderer;
import aegis.rhi;
import aegis.scripting;

export namespace aegis
{
/// @brief The ImGui context and both its backends, for the engine's lifetime: the platform one (GLFW, attach) and the
///        Vulkan one (rhi::ImGuiBackend, createBackend), which no frame-graph rebuild restarts. It also holds the UI's
///        place in the frame: beginFrame opens it after the input was read, endFrame closes it (ImGui::Render) before
///        the frame graph's UI pass records it through the backend (backend). The textures the UI shows are registered
///        with the backend (uiTexture).
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
    /// @brief Opens the frame. Headless, the display is 'extent' and time advances by 'deltaSeconds'.
    void beginFrame(rhi::Extent2D extent, float deltaSeconds);
    /// @brief Ends the frame: its draw data is ready for the UI pass.
    void endFrame();

    /// @brief Starts ImGui's Vulkan backend, once the device exists, and what uiTexture registers with.
    [[nodiscard]] auto createBackend(const rhi::Context& context, rhi::Device& device, std::uint32_t framesInFlight) -> std::expected<void, std::string>;
    /// @brief The backend the frame graph's UI pass records through (UiPass::Desc::backend); null before createBackend.
    [[nodiscard]] auto backend() noexcept -> rhi::ImGuiBackend*;
    /// @brief An ImTextureID showing 'view' (an image of the frame graph, say) with ImGui::Image, valid in this
    ///        frame only: ask again in every frame that shows it. 0 before createBackend.
    /// @note The UI pass samples it: the image must be in a shader read state by then, as a view's "Final" is.
    [[nodiscard]] auto uiTexture(rhi::ImageViewHandle view) -> std::uint64_t;
    /// @brief Releases every texture uiTexture handed out. The GPU must be idle: call it before the views the UI shows
    ///        are recreated (the frame graph is resized or rebuilt).
    void releaseTextures();
    /// @brief Releases the textures and shuts the Vulkan backend down, while the device exists.
    void destroyBackend();

private:
    struct Textures; // The registrations, kept out of this interface

    bool m_glfw{ false }; // ImGui's GLFW backend is initialized
    std::unique_ptr<rhi::ImGuiBackend> m_backend;
    std::unique_ptr<Textures> m_textures;
};

/// @brief A command-line option parseArgs leaves to the shell: its name as typed ("--editor-select") and the token after
///        it, unless that starts with "--".
struct ShellOption
{
    std::string name;
    std::optional<std::string> value;

    auto operator==(const ShellOption&) const -> bool = default;
};

/// @brief One option in the help text: its name with its argument ("--frames <n>") and what it does, a line break
///        (a newline) between the lines of a long description.
struct OptionHelp
{
    std::string_view name;
    std::string_view description;
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
    /// @brief With a shell's views: the screenshot shows each view's target only, not the window's whole frame
    ///        with the shell's UI over it. One view's goes to 'screenshot'; with several, each view's goes next to it,
    ///        named after the view (`shot-Play.png` for `shot.png`).
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
    /// @brief --help: print the help text (helpText) instead of running.
    bool help{ false };
    /// @brief The options parseArgs doesn't know, in their order: the shell's to read in Shell::onStartup (the editor's
    ///        --editor-* options). The runtime's GameShell rejects any.
    std::vector<ShellOption> shellOptions;
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
    ProjectFailed,
    AudioFailed,
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

/// @brief Reads --help, --headless, --frames <n>, --screenshot <png>, --width <n>, --height <n>, --project <dir>,
///        --scene <path> and --dump-asset <id>; the first argument (the program name) is skipped. Any other `--name`
///        goes to AppDesc::shellOptions, with the next token as its value unless that starts with "--". A headless run
///        without --frames stops after 600 frames.
[[nodiscard]] auto parseArgs(std::span<const char* const> args) -> std::expected<AppDesc, EngineError>;

/// @brief The same for main's argc and argv.
[[nodiscard]] auto parseArgs(int argc, char** argv) -> std::expected<AppDesc, EngineError>;

/// @brief The --help text, laid out like clang-format's: OVERVIEW, USAGE naming 'program', then OPTIONS, parseArgs's
///        and the shell's ('shellOptions', such as editor::optionsHelp) sorted by name, each description in one column.
[[nodiscard]] auto helpText(std::string_view program, std::string_view overview, std::span<const OptionHelp> shellOptions = {}) -> std::string;

/// @brief What follows a command-line error: a pointer to 'program' --help.
[[nodiscard]] auto tryHelp(std::string_view program) -> std::string;

/// @brief For a shell that takes no options (GameShell): EngineErrorCode::InvalidArguments naming the first one, if any.
[[nodiscard]] auto rejectShellOptions(std::span<const ShellOption> options) -> std::expected<void, EngineError>;

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
    /// @brief Declares the passes that render a main view's scene into "Final"; null for the engine's
    ///        (render_graph::addScenePasses). Shadow views always get the engine's (render_graph::addShadowPasses). A game adds its passes around those (calling addScenePasses itself) or
    ///        replaces them; the shell's passes and the output passes (UI, present) follow (buildFrameGraph). Runs on
    ///        every rebuild of the frame graph, once per main view in its scope ('view'): it only declares passes and keeps
    ///        no state.
    auto (*buildGraph)(render_graph::GraphBuilder& graph, const render_graph::DefaultPassesDesc& desc, const render_graph::ViewContext& view) -> void{ nullptr };
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

    /// @brief Names the view's resources in the frame graph (its scope, render_graph::scopedName): its image is
    ///        "<name>/Final". Unique among the shell's views, without a '/'; empty is render_graph::names::mainView.
    std::string name;
    /// @brief Replaces the world's main camera this frame; none renders through the main camera.
    std::optional<Camera> camera;
    /// @brief The target's size, apart from the window's; at least 1x1. The scene renders into the view's "Final" at
    ///        this size, and the window shows only the UI.
    rhi::Extent2D extent;
};

/// @brief A view the frame graph renders into a target of its own this frame (Shell::renderViews).
struct ViewTarget
{
    std::string name;
    rhi::Extent2D extent;
    /// @brief The view's image in the frame graph: "<name>/Final", in a state the UI can show (Presentation::uiTexture).
    render_graph::ResourceId final;
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
    /// @brief The views the frame graph renders this frame (the shell's last renderViews), in that order; empty when
    ///        it renders onto the window. Only their images are in a state the UI can show.
    std::span<const ViewTarget> views;
    Presentation& presentation;
    const renderer::Renderer& renderer;
    /// @brief The time since the last frame, which the UI runs by; each world updates by its own (World::clock).
    float deltaSeconds;

    /// @brief Updates 'world', one of the engine's, in 'mode' this frame, by its own clock, once onFrame has returned
    ///        (so this frame's edits show). A world begins with its first update. A world the shell doesn't update
    ///        stays as it is, and its clock stands still. A second call for the same world replaces the mode.
    void update(world::World& world, world::UpdateMode mode);
    /// @brief Renders 'world', one of the engine's, this frame instead of the main world, as its last update left it.
    void render(world::World& world);
    /// @brief The instances of 'world', one of the engine's, as they were last rendered: a world that hasn't rendered
    ///        since it changed shows its earlier instances.
    [[nodiscard]] auto instances(const world::World& world) const -> const render_graph::InstanceStore&;
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

    /// @brief Runs once Engine::run starts: after the project, the game's types and its startup scene have loaded, and
    ///        before the frame graph is built (buildGraph). Reads
    ///        the shell's options (AppDesc::shellOptions); EngineErrorCode::InvalidArguments for an unknown or malformed one
    ///        makes run() return exitUsage.
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
    /// @brief The views to render, each into a target of its own; none renders the main camera onto the window (or,
    ///        headless, the offscreen target). Asked before onFrame for the frame graph, which is rebuilt when the list
    ///        of view names changes and resized when a view's extent changes, and after it for the cameras: a change
    ///        of the views the UI makes applies from the next frame, so the graph's textures the UI shows stay valid
    ///        until they are drawn. Keep an extent steady while a panel is being dragged.
    [[nodiscard]] virtual auto renderViews() const -> std::vector<RenderView> { return {}; }
    /// @brief Declares the shell's passes for one main view ('view'), after the game's (GameModule::buildGraph) and
    ///        before the output passes (UI, present), so they can draw onto the view's "Final", such as the editor's
    ///        overlays. None by default. Runs in each view's scope on every rebuild of the frame graph (the views
    ///        changing): it only declares passes and keeps no state.
    virtual void buildGraph(render_graph::GraphBuilder& graph, const render_graph::DefaultPassesDesc& desc, const render_graph::ViewContext& view) {}
    /// @brief Runs before the engine tears down, while the world, the frame graph and ImGui still exist.
    virtual void onShutdown() {}
};

/// @brief The runtime: the game plays in the main world and gets all the input, with no UI around it.
class GameShell final : public Shell
{
public:
    /// @brief Rejects any shell option: the runtime takes none.
    auto onStartup(Engine& engine) -> std::expected<void, EngineError> override;
    void onFrame(FrameContext& frame) override { frame.update(frame.world, world::UpdateMode::Play); }
};

/// @brief The views the frame graph renders (render_graph::DefaultPassesDesc::views), and so the frame scene's
///        (render_graph::frameSceneViews), in that order: a main view for each of 'targets' with its name and extent,
///        or, without targets, render_graph::names::mainView following the output; then their shadow views
///        (render_graph::addShadowViews).
[[nodiscard]] auto graphViews(std::span<const ViewTarget> targets) -> std::vector<render_graph::DefaultPassesDesc::View>;

/// @brief Declares the passes that render the scene, in place of the engine's (GameModule::buildGraph).
using GraphBuild = std::function<void(render_graph::GraphBuilder& graph, const render_graph::DefaultPassesDesc& desc, const render_graph::ViewContext& view)>;

/// @brief Declares the frame graph's passes in the engine's order: the passes the views share
///        (render_graph::addSharedPasses); for each view of desc.views in render_graph::viewDeclarationOrder (the spot
///        shadow views first), in its scope, for a main view the game's ('game', or render_graph::addScenePasses if it
///        is empty) and the shell's (Shell::buildGraph), and for a shadow view the engine's
///        (render_graph::addShadowPasses); then the output passes (render_graph::addOutputPasses).
auto buildFrameGraph(render_graph::GraphBuilder& graph, const render_graph::DefaultPassesDesc& desc, const GraphBuild& game,
    Shell& shell) -> void;

/// @brief parseArgs plus Engine::create for a 'main': a failure is logged and comes back as the exit code
///        to return (exitUsage for a bad command line, exitFailure when the engine can't start). The shell's options
///        are left to the shell's onStartup.
[[nodiscard]] auto createEngine(int argc, char** argv) -> std::expected<std::unique_ptr<Engine>, int>;

class Engine
{
public:
    static constexpr std::uint32_t FRAMES_IN_FLIGHT{ aegis::renderer::Renderer::maxFramesInFlight };

    /// @brief Creates the window (unless headless), the device, renderer and assets. The frame graph follows in run().
    [[nodiscard]] static auto create(const AppDesc& desc = {}) -> std::expected<std::unique_ptr<Engine>, EngineError>;

    Engine(const Engine&) = delete;
    Engine(Engine&&) = delete;
    ~Engine();

    auto operator=(const Engine&) -> Engine& = delete;
    auto operator=(Engine&&) -> Engine& = delete;

    /// @brief GPU-driven rendering (mesh shaders) when AppDesc asks for it and the device supports it, CPU-driven otherwise.
    [[nodiscard]] auto useGpuDrivenRendering() const -> bool;

    /// @brief The registered components and scripts, the built-in ones included. Register a game's
    ///        components (world::registerComponent) and scripts (scripting::registerScript) here before loadScene.
    [[nodiscard]] auto types() -> reflect::TypeRegistry& { return m_types; }

    /// @brief Starts the shell, builds the frame graph (buildFrameGraph) and runs until the window closes or
    ///        AppDesc::frames have rendered.
    /// @return An exit code for main: exitSuccess, or exitFailure after a frame graph that fails to compile, a failed
    ///         frame, a failed screenshot or a validation error. The cause is logged.
    [[nodiscard]] auto run() -> int;

    /// @brief The audio every world plays through: a device when windowed, none when headless (the mix advances by
    ///        frame time; see audio::AudioEngine::Desc::device). Its stats count the sounds started and playing.
    [[nodiscard]] auto audio() -> audio::AudioEngine& { return *m_audio; }

    /// @brief Finds assets by path and loads them by ID.
    [[nodiscard]] auto assets() -> AssetLibrary& { return *m_assets; }

    /// @brief The passes' pipelines and BRDF LUT, which every frame graph the engine builds shares.
    [[nodiscard]] auto pipelines() const -> const render_graph::PipelineLibrary& { return *m_pipelines; }

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

    /// @brief Replaces the passes that render the scene with the game's (see GameModule::buildGraph, which the
    ///        launcher sets); empty for the engine's. Set it before run(), which builds the frame graph.
    void setGraphBuild(GraphBuild build) { m_graphBuild = std::move(build); }

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

    /// @brief The last frame's image of the view 'name' (FrameContext::views) as RGBA8 rows, its "Final" rounded,
    ///        read back after waiting for the GPU. For checks, at the safe point (Shell::onFrame); headless only.
    [[nodiscard]] auto readView(std::string_view name) -> std::expected<std::vector<std::byte>, std::string>;

    /// @brief Replaces the shell (GameShell by default), such as with the editor (editor::EditorShell). Call it
    ///        before run().
    void setShell(std::unique_ptr<Shell> shell);

    /// @brief The project folder (AppDesc::project); empty for the samples.
    [[nodiscard]] auto project() const -> const std::filesystem::path& { return m_desc.project; }
    /// @brief How the app was started.
    [[nodiscard]] auto desc() const -> const AppDesc& { return m_desc; }

    /// @brief No window, input or platform backend: frames render offscreen (AppDesc::headless).
    [[nodiscard]] auto headless() const -> bool { return !m_window; }

    /// @brief The input every world's scripts read when headless: the project's input map, and nothing down until a
    ///        check applies events to it (beginFrame, then apply, at the safe point before the worlds update). Null
    ///        with a window, whose input comes from the platform.
    [[nodiscard]] auto headlessInput() -> platform::InputState* { return m_window ? nullptr : &m_headlessInput; }

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
    auto createAudio() -> std::expected<void, EngineError>;
    /// @brief Reads the project file and hands its input map to the input (or the headless state).
    auto loadProjectFile() -> std::expected<void, EngineError>;
    /// @brief Replaces the frame graph with one rendering the main camera onto the renderer's target, or, with
    ///        'views', each of them into a target of its own. Frames in flight must have finished.
    auto createFrameGraph(std::vector<ViewTarget> views = {}) -> std::expected<void, EngineError>;
    bool resize();
    /// @brief Rebuilds or resizes the frame graph for the shell's views, if they changed since the last frame.
    bool followViews(const std::vector<RenderView>& views);
    /// @brief Gives every world's main camera the aspect of what it renders into: the first of 'views' without a
    ///        camera of its own, or the window.
    void updateCameraAspect(const std::vector<RenderView>& views);
    /// @brief This frame's scene of the world 'shown': the main camera's view, or one per view the graph renders,
    ///        through 'views' camera of the same name if it has one. A main view whose camera comes from elsewhere
    ///        than last frame's (another world, main camera or shell camera) is a camera cut (FrameScene::Camera::cut).
    auto frameScene(world::World& shown, const render_graph::InstanceStore& instances, const std::vector<RenderView>& views)
        -> render_graph::FrameScene;
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
    /// @brief Reads 'image' back as RGBA8: the offscreen target, or a view's "Final" (RGBA16F, rounded). The device is idle.
    auto readBack(const renderer::Texture& image, rhi::Extent2D extent, bool view) -> std::expected<std::vector<std::byte>, std::string>;

    /// @brief Longest frame time the scene and scripts see.
    static constexpr float MAX_FRAME_DELTA_SEC{ 0.1f };

    // Order matters: members are destroyed bottom up, and everything holding GPU resources must go
    // before the device.
    AppDesc m_desc;
    std::unique_ptr<aegis::platform::Window> m_window; // null when headless
    // Before ImGui's GLFW backend installs its callbacks, which chain to the window's. Null when headless.
    std::unique_ptr<aegis::platform::Input> m_input;
    aegis::platform::InputState m_headlessInput; // What scripts read when headless: the input map, and what checks apply
    Presentation m_presentation;                 // The ImGui context: outlives the frame graph's UI pass and the shell
    std::uint32_t m_lastFrameIndex{ 0 };         // The frame slot the last frame drew into
    std::unique_ptr<aegis::rhi::Context> m_context;
    std::unique_ptr<aegis::rhi::Device> m_device;
    std::unique_ptr<aegis::renderer::Renderer> m_renderer;
    std::optional<AssetLibrary> m_assets;
    reflect::TypeRegistry m_types;
    std::unique_ptr<scripting::Spawner> m_spawner;                              // What scripts spawn through: every world's registry context
    std::unique_ptr<audio::AudioEngine> m_audio;                                // Outlives the worlds, whose AudioSystems play through it
    core::FrameClock m_clock{ core::FrameClock::clamped(MAX_FRAME_DELTA_SEC) }; // The UI's; each world has its own
    std::function<void(world::World&)> m_worldSetup;                            // The game's systems
    GraphBuild m_graphBuild;                                                    // The game's scene passes; empty for the engine's
    // Their instances hold meshes and materials: they go before the device.
    Worlds m_worlds{ [this](world::World& world) { setUpWorld(world); } };
    world::World* m_mainWorld{ nullptr };          // The first of m_worlds
    std::vector<WorldUpdate> m_updates;            // This frame's, from the shell (FrameContext::update)
    world::World* m_renderRequest{ nullptr };      // This frame's, from the shell (FrameContext::render); none for the main world
    const world::World* m_lastRendered{ nullptr }; // The world whose instances the passes' buffers hold
    /// @brief Where a main view's camera came from (frameScene): a world's main camera, or the shell's camera of it.
    struct CameraSource
    {
        const world::World* world{ nullptr }; // None: the shell's
        world::Entity mainCamera;

        auto operator==(const CameraSource&) const -> bool = default;
    };
    std::unordered_map<std::string, CameraSource> m_cameraSources; // Last frame's, by view name; a view without a camera has none
    render_graph::FrameSceneSource m_frameScene;                   // Where the passes read the frame scene; outlives the graph
    // The passes' pipelines and BRDF LUT: outlive every frame graph, so a rebuild (followViews) creates none.
    std::unique_ptr<render_graph::PipelineLibrary> m_pipelines{ std::make_unique<render_graph::PipelineLibrary>() };
    // The passes' settings, which the renderer panel edits: outlive every frame graph, so a rebuild keeps them.
    render_graph::RenderSettings m_renderSettings;
    std::unique_ptr<aegis::render_graph::FrameGraph> m_frameGraph;
    std::vector<ViewTarget> m_views; // The views the frame graph renders into targets of their own; none: onto the target
    bool m_quit{ false };            // quit(): run() ends after this frame
    // Last, so it goes first, while everything it may reference (the worlds, the frame graph, ImGui) exists.
    std::unique_ptr<Shell> m_shell{ std::make_unique<GameShell>() };
};
}
