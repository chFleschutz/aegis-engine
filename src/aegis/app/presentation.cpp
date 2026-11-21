module;

#include <cassert>

#include <imgui.h>
#include <imgui_impl_glfw.h>

module aegis.engine;

import std;
import aegis.render_passes;

namespace aegis
{
namespace
{
constexpr float FALLBACK_DELTA_SEC{ 1.0f / 60.0f }; // ImGui needs time to pass in every frame
}

struct Presentation::Textures
{
    render_graph::UiTextures registered;
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
    // Its registrations went with the UI pass's backend; the engine destroys it while the device exists.
    assert(!m_textures && "The UI textures must be destroyed before the device");
    // The backends go before the context; the frame graph's UI pass (the Vulkan backend) is already gone.
    if (m_glfw)
        ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Presentation::attach(platform::Window& window)
{
    ImGui_ImplGlfw_InitForVulkan(window.glfwWindow(), true);
    m_glfw = true;
}

void Presentation::detach()
{
    if (m_glfw)
        ImGui_ImplGlfw_Shutdown();
    m_glfw = false;
}

void Presentation::beginFrame(rhi::Extent2D extent, float deltaSeconds)
{
    if (m_textures)
        m_textures->registered.nextFrame();
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
    return m_textures ? m_textures->registered.get(view) : 0;
}

auto Presentation::createTextures(rhi::Device& device) -> std::expected<void, std::string>
{
    auto textures = render_graph::UiTextures::create(device);
    if (!textures)
        return std::unexpected{ describe(textures.error()) };
    m_textures = std::make_unique<Textures>(std::move(*textures));
    return {};
}

void Presentation::releaseTextures()
{
    if (m_textures)
        m_textures->registered.releaseAll();
}

void Presentation::destroyTextures()
{
    m_textures.reset();
}
}
