module;

#include <imgui.h>

// The aegis-test harness: engine.cpp sets up the device and renders frames, checks/*.cpp hold the checks it
// runs, one file per group, and main.cpp parses the arguments.
export module aegis.test;

import std;
import aegis.asset_library;
import aegis.assets;
import aegis.platform;
import aegis.reflect;
import aegis.renderer;
import aegis.render_graph;
import aegis.rhi;
import aegis.world;
import aegis.world.registry;

using SpirvBuffer = std::vector<std::uint32_t>;

// Exit code and error text for a run the device can't do (CTest SKIP_RETURN_CODE in tests/CMakeLists.txt).
export constexpr int skipExitCode{ 77 };
export constexpr std::string_view skipMeshShaders{ "Skipped: --gpu-driven needs mesh shaders, which this device lacks" };

auto loadSPIRV(const std::filesystem::path& path) -> std::expected<SpirvBuffer, std::string>;

export namespace aegis
{
struct Options
{
    bool headless{ false };
    std::uint32_t frames{ 0 }; // 0: until the window is closed
    bool scene{ false };       // Render a loaded scene through the render graph instead of the triangle
    bool gpuDriven{ false };   // With --scene: the GPU-driven (mesh shader) geometry path
    bool mutateScene{ false }; // With --scene: change the loaded scene while frames are in flight (see mutateScene())
    bool mirror{ false };      // With --scene: mirror the helmet (a scale of -1 on x), which must not turn it inside out
    std::string screenshot;    // Headless: save the last frame here as PNG
    std::string compare;       // Headless: fail unless the last frame matches this reference PNG (see compareToReference())
    int tolerance{ 0 };        // With --compare: the largest per-channel difference (0-255) a pixel may have
    bool noOverlay{ false };   // With --scene: leave the stats window out, its per-frame timings change every run
};

class Engine
{
public:
    // engine.cpp
    static auto create(const Options& options) -> std::expected<Engine, std::string>;
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

    // checks/input.cpp
    auto verifyInput() -> std::expected<void, std::string>;

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
    auto verifyGraphRebuild(bool gpuDriven) -> std::expected<void, std::string>;

    // checks/scene.cpp
    auto createScene() -> std::expected<void, std::string>;
    auto instantiateModel(std::string_view path) -> std::expected<aegis::world::Entity, std::string>;
    static auto buildUi(const renderer::Renderer& renderer, const render_graph::FrameGraph& frameGraph, bool overlay) -> void;
    auto mutateScene(std::uint32_t frame) -> std::expected<void, std::string>;

private:
    // One pool per frame in flight, so a frame's timestamps are read only after the GPU finished it.
    struct FrameTimer
    {
        rhi::QueryPool pool;
        bool pending{ false };
    };

    Engine() = default;

    // engine.cpp
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
    auto compareToReference(const std::vector<std::byte>& pixels, rhi::Extent2D extent) const -> std::expected<void, std::string>;
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

    // --scene only. Declared last: destroyed first, while the device they free into still exists.
    reflect::TypeRegistry m_types; // The components a model's scene holds
    std::optional<AssetLibrary> m_assets;
    std::unique_ptr<aegis::world::World> m_scene;
    aegis::world::Entity m_helmet;                                  // The holder of the model instance loaded with the scene
    aegis::world::Entity m_addedModel;                              // --mutate-scene: the holder of the model instance loaded later
    std::uint32_t m_staticDrawn{ 0 };                               // --mutate-scene: drawable static entities before the last change
    std::unique_ptr<render_graph::DrawBatchRegistry> m_drawBatches; // Disconnects before the scene goes
    struct ImGuiContextDeleter
    {
        auto operator()(ImGuiContext* context) const -> void { ImGui::DestroyContext(context); }
    };
    using ImGuiContextHandle = std::unique_ptr<ImGuiContext, ImGuiContextDeleter>;
    ImGuiContextHandle m_imgui; // Outlives the frame graph's UI pass
    std::unique_ptr<render_graph::FrameGraph> m_frameGraph;
};
}
