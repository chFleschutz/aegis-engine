module;

#include <cassert>

#include <imgui.h>
#include <imgui_impl_glfw.h>

module aegis.engine;

import std;
import aegis.platform;
import aegis.render_graph;
import aegis.render_passes;
import aegis.renderer;
import aegis.rhi;

namespace aegis
{
namespace
{
constexpr float FALLBACK_DELTA_SEC{ 1.0f / 60.0f }; // ImGui needs time to pass in every frame
}

// Image views the UI shows, registered with the backend. A registration no frame asked for during the frames in flight
// is released, and the IDs change whenever registrations are released.
struct Presentation::Textures
{
    struct Registration
    {
        rhi::ImageViewHandle view;
        std::uint64_t id;
        std::uint64_t lastFrame; // The last frame that asked for it
    };

    rhi::Device* device;
    rhi::ImGuiBackend* backend;
    rhi::SamplerHandle sampler;
    std::uint32_t framesInFlight;
    std::vector<Registration> registrations; // Few, so searched in order
    std::uint64_t frame{ 0 };

    Textures(rhi::Device& device, rhi::ImGuiBackend& backend, rhi::SamplerHandle sampler, std::uint32_t framesInFlight) :
        device{ &device },
        backend{ &backend },
        sampler{ sampler },
        framesInFlight{ framesInFlight }
    {
    }

    ~Textures()
    {
        releaseAll();
        device->free(sampler);
    }

    auto get(rhi::ImageViewHandle view) -> std::uint64_t
    {
        const auto found = std::ranges::find(registrations, view, &Registration::view);
        if (found != registrations.end())
        {
            found->lastFrame = frame;
            return found->id;
        }
        const auto id = backend->addTexture(*device, sampler, view);
        if (id != 0)
            registrations.push_back({ .view = view, .id = id, .lastFrame = frame });
        return id;
    }

    auto nextFrame() -> void
    {
        ++frame;
        // A frame waits for the one framesInFlight before it, so as this one starts the last framesInFlight
        // frames may still run; older ones are done.
        const auto done = [this](const Registration& registration) {
            return frame - registration.lastFrame > framesInFlight;
        };
        for (const auto& registration : registrations)
        {
            if (done(registration))
                backend->removeTexture(registration.id);
        }
        std::erase_if(registrations, done);
    }

    auto releaseAll() -> void
    {
        for (const auto& registration : registrations)
            backend->removeTexture(registration.id);
        registrations.clear();
    }
};

Presentation::Presentation(const Desc& desc) :
    m_device{ &desc.device },
    m_renderer{ &desc.renderer },
    m_gpuDriven{ desc.gpuDriven },
    m_declare{ desc.declare }
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr; // A shell that keeps a layout names its file
#ifndef NDEBUG
    io.ConfigDebugIsDebuggerPresent = true;
#endif // !NDEBUG
}

auto Presentation::create(const Desc& desc) -> std::expected<std::unique_ptr<Presentation>, EngineError>
{
    std::unique_ptr<Presentation> presentation{ new Presentation{ desc } };
    if (desc.window)
    {
        // The window's input installed its callbacks first: ImGui's chain to them.
        ImGui_ImplGlfw_InitForVulkan(desc.window->glfwWindow(), true);
        presentation->m_glfw = true;
    }

    auto backend = rhi::ImGuiBackend::create(desc.context, desc.device, { .framesInFlight = desc.framesInFlight });
    if (!backend)
        return std::unexpected{ makeError(EngineErrorCode::RendererFailed, "Failed to initialize ImGui's Vulkan backend") };
    // Filtered, as the UI rarely shows a texture at its own size; clamped, so the edges don't wrap.
    auto sampler = desc.device.createSampler({
        .name = "UI Textures",
        .addressMode = rhi::SamplerAddressMode::ClampToEdge,
        .maxAnisotropy = 1.0f,
    });
    if (!sampler)
        return std::unexpected{ makeError(EngineErrorCode::RendererFailed, "Failed to create the UI textures' sampler") };
    presentation->m_backend = std::move(*backend);
    presentation->m_textures = std::make_unique<Textures>(desc.device, *presentation->m_backend, *sampler, desc.framesInFlight);
    return presentation;
}

Presentation::~Presentation()
{
    // What goes through ImGui's Vulkan backend (the frame graph's UI pass, the textures), the backend, then the
    // platform one, then the context. The device outlives this.
    m_frameGraph.reset();
    m_textures.reset();
    m_backend.reset();
    if (m_glfw)
        ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Presentation::beginFrame(rhi::Extent2D extent, float deltaSeconds)
{
    if (m_textures)
        m_textures->nextFrame();
    if (m_glfw)
    {
        ImGui_ImplGlfw_NewFrame();
    }
    else
    {
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2{ static_cast<float>(extent.x), static_cast<float>(extent.y) };
        io.DeltaTime = deltaSeconds > 0.0f ? deltaSeconds : FALLBACK_DELTA_SEC;
    }
    ImGui::NewFrame();
}

void Presentation::endFrame()
{
    ImGui::Render();
}

auto Presentation::uiTexture(rhi::ImageViewHandle view) -> std::uint64_t
{
    return m_textures->get(view);
}

void Presentation::releaseTextures()
{
    m_textures->releaseAll();
}

auto Presentation::buildGraph(std::vector<ViewTarget> views) -> std::expected<void, EngineError>
{
    m_views = std::move(views);
    m_frameGraph.reset(); // Frees the old graph's resources before the new one creates its own
    render_graph::DefaultPassesDesc desc{
        .scene = m_frameScene,
        .pipelines = *m_pipelines,
        .settings = m_renderSettings,
        .gpuDriven = m_gpuDriven,
        .targetFormat = m_renderer->targetFormat(),
        .ui = true, // The shell's UI, empty for a game; headless too, for screenshots of it
        .uiBackend = m_backend.get(),
        .views = graphViews(m_views), // Engine::frameScene gives the scene the same views
        // Without views of its own the main camera renders onto the target, following it.
        .shownViews = !m_views.empty(),
    };
    render_graph::GraphBuilder builder;
    m_declare(builder, desc);
    auto created = render_graph::FrameGraph::create(*m_device, std::move(builder), m_renderer->extent());
    if (!created)
        return std::unexpected{ makeError(EngineErrorCode::FrameGraphFailed, std::format("Failed to compile the frame graph: {}", describe(created.error()))) };
    m_frameGraph = std::move(*created);
    for (auto& view : m_views)
    {
        const auto final = m_frameGraph->declarations().find(render_graph::scopedName(view.name, render_graph::names::finalImage));
        if (!final)
            return std::unexpected{ makeError(EngineErrorCode::FrameGraphFailed, std::format("The frame graph's view '{}' has no \"Final\"", view.name)) };
        view.final = *final;
    }
    return {};
}

auto Presentation::followViews(const std::vector<RenderView>& views) -> std::expected<void, EngineError>
{
    std::vector<ViewTarget> wanted;
    for (const auto& view : views)
    {
        auto name = view.name.empty() ? std::string{ render_graph::names::mainView } : view.name;
        if (name.contains('/') || std::ranges::contains(wanted, name, &ViewTarget::name))
            return std::unexpected{ makeError(EngineErrorCode::FrameGraphFailed, std::format("The shell's view name '{}' is used twice or has a '/'", name)) };
        wanted.push_back({ .name = std::move(name), .extent = { std::max(view.extent.x, 1u), std::max(view.extent.y, 1u) } });
    }
    const bool sameViews = std::ranges::equal(wanted, m_views, {}, &ViewTarget::name, &ViewTarget::name);
    if (sameViews && std::ranges::equal(wanted, m_views, {}, &ViewTarget::extent, &ViewTarget::extent))
        return {};

    // Frames in flight still use the graph's textures (and, rebuilding, its passes), the UI's registrations too.
    m_device->waitIdle();
    releaseTextures();
    if (!sameViews)
        return buildGraph(std::move(wanted));
    // The same views, some at a new extent: only their textures are recreated.
    for (std::size_t i = 0; i < wanted.size(); ++i)
    {
        if (wanted[i].extent == m_views[i].extent)
            continue;
        m_views[i].extent = wanted[i].extent;
        if (auto resized = m_frameGraph->resized(m_views[i].name, m_views[i].extent); !resized)
            return std::unexpected{ makeError(EngineErrorCode::FrameGraphFailed, std::format("Failed to resize the view '{}': {}", m_views[i].name, describe(resized.error()))) };
    }
    return {};
}

auto Presentation::targetResized() -> std::expected<void, EngineError>
{
    if (!m_views.empty())
        return {}; // The graph follows the views, not the target
    // The graph's views are about to be recreated.
    releaseTextures();
    if (auto resized = m_frameGraph->resized(m_renderer->extent()); !resized)
        return std::unexpected{ makeError(EngineErrorCode::FrameGraphFailed, std::format("Failed to resize the frame graph: {}", describe(resized.error()))) };
    return {};
}
}
