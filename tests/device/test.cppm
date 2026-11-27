// The aegis-test harness: harness.cpp sets up the device and renders the triangle, checks/*.cpp hold the checks
// it runs, one file per group, checks/scene.cpp runs --scene through the app (aegis::Engine), checks/gameplay.cpp runs
// --gameplay through it with the Examples game, and main.cpp parses the arguments.
export module aegis.test;

import std;
import aegis.platform;
import aegis.renderer;
import aegis.rhi;

using SpirvBuffer = std::vector<std::uint32_t>;

// Exit code and error text for a run the device can't do (CTest SKIP_RETURN_CODE in tests/CMakeLists.txt).
export constexpr int skipExitCode{ 77 };
export constexpr std::string_view skipMeshShaders{ "Skipped: --gpu-driven needs mesh shaders, which this device lacks" };

auto loadSPIRV(const std::filesystem::path& path) -> std::expected<SpirvBuffer, std::string>;

export namespace aegis::test
{
// DamagedHelmet under the Kloppenheim sky, a camera and three lights, in the sample project: what --scene loads.
inline constexpr std::string_view testSceneFile{ "scenes/tests/aegis_test.scene" };
// A looping, spatial AudioSource that plays on begin, in the sample project: what --audio loads.
inline constexpr std::string_view audioSceneFile{ "scenes/audio.scene" };
// Dynamic boxes dropped onto a static floor, in the sample project: what --physics loads.
inline constexpr std::string_view physicsSceneFile{ "scenes/physics.scene" };
// A character walking into five boxes, a spawner, a moving platform and a contact sound, in the sample project, played
// by the Examples game's scripts: what --gameplay runs.
inline constexpr std::string_view gameplaySceneFile{ "scenes/gameplay.scene" };

struct Options
{
    bool headless{ false };
    std::uint32_t frames{ 0 }; // 0: until the window is closed
    bool scene{ false };       // Render the test scene file through the app instead of the triangle (runScene)
    // With --scene: the scene file, relative to the sample project's content
    std::string sceneFile{ testSceneFile };
    bool gpuDriven{ false };    // With --scene: the GPU-driven (mesh shader) geometry path
    bool mutateScene{ false };  // With --scene: change the loaded scene while frames are in flight (see SceneShell::mutateScene)
    bool mirror{ false };       // With --scene: mirror the helmet (a scale of -1 on x), which must not turn it inside out
    bool unlit{ false };        // With --scene: an unlit cube next to a PBR one, whose colour the last frame must show
                                // unlit (see SceneShell::addUnlitCubes)
    bool blend{ false };        // With --scene: two overlapping half-transparent cubes in front of the helmet, drawn by the
                                // forward pass alone, back to front (see SceneShell::addBlendedCubes)
    bool blendSwap{ false };    // With --blend: the two cubes swap places, so the one added first is the farther
    bool doubleSided{ false };  // With --scene: an unlit, double-sided plane with its back to the main camera, whose colour
                                // the last frame must show (see SceneShell::addBackFacingPlane)
    std::string screenshot;     // Headless: save the last frame here as PNG
    std::string compare;        // Headless: fail unless the last frame matches this reference PNG (see compareToReference())
    int tolerance{ 0 };         // With --compare: the largest per-channel difference (0-255) a pixel may have
    bool noOverlay{ false };    // With --scene: leave the stats window out, its per-frame timings change every run
    bool view{ false };         // With --scene: render through a second camera into a view of its own size, which
                                // --screenshot and --compare then read (see viewExtent)
    bool views{ false };        // With --scene: render two views of different sizes in one frame, the main camera's and a
                                // second camera's, and check each against the view rendered alone (see SceneShell::checkViews);
                                // --screenshot then writes one PNG per view
    bool worlds{ false };       // With --scene: render a second world from the scene file in the middle third of the
                                // frames (see SceneShell::switchWorlds)
    bool postProcess{ false };  // With --scene: give the main camera a PostProcess (postProcessSettings), and rebuild the
                                // frame graph halfway (a view for the first half of the frames, none after it)
    bool noSamples{ false };    // Skip the checks that load the sample project (games/examples/project/ not checked out)
    bool audio{ false };        // Run the audio scene (audioSceneFile) and check that its one sound started and still plays
    bool physics{ false };      // Run the physics scene (physicsSceneFile) and check that its dynamic bodies fell, stayed above the
                                // floor and came to rest (see SceneShell::watchPhysics)
    bool spawn{ false };        // With --scene: a script spawns the engine's cube and destroys it again (see SceneShell::checkSpawn)
    bool materialFile{ false }; // Headless: a scene in a temporary project renders with its material file, and again after the
                                // file changed (see runMaterialFile)
    bool hotReload{ false };    // Headless: a texture of a temporary project changed, deleted and written back while the engine
                                // runs reaches the frame through a rescan and a reload (see runHotReload)
    bool gameplay{ false };     // Headless: run the gameplay scene (gameplaySceneFile) for the same simulated time at three frame
                                // times with scripted input, and check that it plays the same (see runGameplay)
};

// --scene: the target's size. --view: the view's size, unlike the target's, and the one it is resized to halfway.
// --views: the main view has viewExtent, the second camera's view sideViewExtent.
inline constexpr rhi::Extent2D sceneExtent{ 800, 600 };
inline constexpr rhi::Extent2D viewExtent{ 480, 270 };
inline constexpr rhi::Extent2D resizedViewExtent{ 400, 300 };
inline constexpr rhi::Extent2D sideViewExtent{ 320, 240 };
// --gameplay: the target's size, small since it renders about 1400 frames.
inline constexpr rhi::Extent2D gameplayExtent{ 320, 240 };

// checks/scene.cpp: --scene, the test scene file loaded and run by aegis::Engine (see the Options). Returns the exit code.
auto runScene(const Options& options) -> int;
// checks/gameplay.cpp: --gameplay, the gameplay scene at three frame times (see the Options). Returns the exit code.
auto runGameplay(const Options& options) -> int;

// checks/material_file.cpp: --material-file, a scene that names a material file rendered three times by fresh engines: the
// file, the file changed, and an edited copy applied to the loaded material. Needs no sample project. Returns the exit code.
auto runMaterialFile(const Options& options) -> int;
// checks/hot_reload.cpp: --hot-reload, a texture of a temporary project rewritten, deleted and written back while one engine
// renders it, each change rescanned and reloaded (AssetLibrary::reload). Needs no sample project. Returns the exit code.
auto runHotReload(const Options& options) -> int;

// The triangle and the device checks, on a device of its own: they test the RHI under the engine, not its startup.
class Harness
{
public:
    // harness.cpp
    static auto create(const Options& options) -> std::expected<Harness, std::string>;
    auto run() -> int;
    auto drawFrame(const renderer::Renderer::FrameInfo& frameInfo) -> void;
    auto depthDesc() const -> renderer::Texture::Desc;

    // checks/uploads.cpp
    auto verifyUploads() -> std::expected<void, std::string>;
    auto verifyMipGeneration() -> std::expected<void, std::string>;

    // checks/rhi.cpp
    auto verifySamplers() -> std::expected<void, std::string>;
    auto verifyBufferDeviceAddress() -> std::expected<void, std::string>;
    auto verifyComputeRoundTrip() -> std::expected<void, std::string>;
    auto verifyValidationLog() -> std::expected<void, std::string>;

    // checks/input.cpp
    auto verifyInput() -> std::expected<void, std::string>;

    // checks/window.cpp
    auto verifyWindow() -> std::expected<void, std::string>;

    // checks/assets.cpp
    auto verifyAssets() -> std::expected<void, std::string>;
    auto verifyAssetLibrary() -> std::expected<void, std::string>;

    // checks/ibl.cpp
    auto verifyIbl() -> std::expected<void, std::string>;
    auto readRgba16f(rhi::ImageHandle image, std::uint32_t size, std::uint32_t layers)
        -> std::expected<std::vector<float>, std::string>;
    static auto halfToFloat(std::uint16_t half) -> float;

    // checks/graph.cpp
    auto verifyGraphTextures() -> std::expected<void, std::string>;
    auto verifyGraphHistory() -> std::expected<void, std::string>;
    auto verifyGraphRebuild(bool gpuDriven) -> std::expected<void, std::string>;
    auto verifyPipelineSharing(bool gpuDriven) -> std::expected<void, std::string>;

private:
    // One pool per frame in flight, so a frame's timestamps are read only after the GPU finished it.
    struct FrameTimer
    {
        rhi::QueryPool pool;
        bool pending{ false };
    };

    Harness() = default;

    // harness.cpp
    auto recordFrameTime(FrameTimer& timer) -> void;
    auto reportFrameTimes() const -> int;
    auto flushAndWait() -> std::expected<void, std::string>;

    // Records 'record' into a one-shot command buffer, submits it, and waits for completion.
    auto submitAndWait(std::string_view name, auto&& record) -> std::expected<void, std::string>
    {
        auto pool = m_device->createCommandPool({
            .name = name,
            .queueFamily = m_device->graphicsQueue().family(),
        });
        if (!pool)
            return std::unexpected{ std::format("{}: failed to create command pool", name) };

        auto cmd = m_device->createCommandBuffer({ .name = name, .pool = *pool });
        if (!cmd)
            return std::unexpected{ std::format("{}: failed to create command buffer", name) };

        cmd->begin(true);
        record(*cmd);
        cmd->end();

        auto timeline = m_device->graphicsQueue().submit(*cmd);
        if (!timeline)
            return std::unexpected{ std::format("{}: failed to submit", name) };
        m_device->graphicsQueue().wait(*timeline);
        return {};
    }

    // checks/uploads.cpp
    auto verifyBufferUpload() -> std::expected<void, std::string>;
    auto verifyOverflowUpload() -> std::expected<void, std::string>;
    auto verifyImageUpload() -> std::expected<void, std::string>;
    auto verifyCubemapUpload() -> std::expected<void, std::string>;
    auto verifySinglePixelUploads() -> std::expected<void, std::string>;
    auto verifyTextureRoundTrip(std::string_view name, rhi::ImageType type, rhi::Extent3D extent,
        rhi::Format format, std::uint32_t mipLevels, std::uint32_t arrayLayers, auto&& fillFor)
        -> std::expected<void, std::string>;

    // checks/offscreen.cpp
    auto readTarget() -> std::expected<std::vector<std::byte>, std::string>;
    auto verifyOffscreenFrame() -> std::expected<void, std::string>;

    Options m_options;
    std::unique_ptr<platform::Window> m_window; // null when headless
    std::unique_ptr<platform::Input> m_input;   // null when headless
    std::unique_ptr<rhi::Context> m_context;
    std::unique_ptr<rhi::Device> m_device;
    std::unique_ptr<renderer::Renderer> m_renderer;

    std::optional<rhi::Pipeline> m_pipeline;
    std::optional<rhi::Pipeline> m_overlayPipeline;
    renderer::Texture m_depthTexture;
    std::vector<FrameTimer> m_frameTimers;
    std::uint32_t m_timedFrames{ 0 };
    std::uint32_t m_frameTimeErrors{ 0 };
    double m_frameTimeTotalMs{ 0.0 };
};
}

namespace aegis::test
{
// checks/offscreen.cpp: --compare, for the triangle and the scene. 'pixels' is the last frame, RGBA8.
auto compareToReference(const Options& options, std::span<const std::byte> pixels, rhi::Extent2D extent) -> std::expected<void, std::string>;
}
