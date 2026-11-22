module;

#include <cassert>

#include <imgui.h>
#include <imgui_impl_glfw.h>

module aegis.engine;

import std;
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

Presentation::Presentation()
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

Presentation::~Presentation()
{
    // The engine destroyed the Vulkan backend while the device existed; the platform one goes before the context.
    assert(!m_backend && "The Vulkan backend must be destroyed before the device");
    if (m_glfw)
        ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Presentation::attach(platform::Window& window)
{
    ImGui_ImplGlfw_InitForVulkan(window.glfwWindow(), true);
    m_glfw = true;
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
    return m_textures ? m_textures->get(view) : 0;
}

auto Presentation::createBackend(const rhi::Context& context, rhi::Device& device, std::uint32_t framesInFlight) -> std::expected<void, std::string>
{
    auto backend = rhi::ImGuiBackend::create(context, device, { .framesInFlight = framesInFlight });
    if (!backend)
        return std::unexpected{ std::string{ "Failed to initialize ImGui's Vulkan backend" } };
    // Filtered, as the UI rarely shows a texture at its own size; clamped, so the edges don't wrap.
    auto sampler = device.createSampler({
        .name = "UI Textures",
        .addressMode = rhi::SamplerAddressMode::ClampToEdge,
        .maxAnisotropy = 1.0f,
    });
    if (!sampler)
        return std::unexpected{ std::string{ "Failed to create the UI textures' sampler" } };
    m_backend = std::move(*backend);
    m_textures = std::make_unique<Textures>(device, *m_backend, *sampler, framesInFlight);
    return {};
}

auto Presentation::backend() noexcept -> rhi::ImGuiBackend*
{
    return m_backend.get();
}

void Presentation::releaseTextures()
{
    if (m_textures)
        m_textures->releaseAll();
}

void Presentation::destroyBackend()
{
    m_textures.reset();
    m_backend.reset();
}
}
