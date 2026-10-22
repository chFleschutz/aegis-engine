module;

#include <imgui.h>
#include <ImGuizmo.h>

module aegis.editor;
import std;
import aegis.core;
import aegis.math;
import aegis.render_passes;
import aegis.rhi;
import aegis.world;
import aegis.world.systems.transform_system;
import :panels;

namespace aegis::editor
{
namespace
{
// Today's look, until the editor's theme (docs/editor.md, "Visual style") replaces it.
void applyStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.PopupRounding = 4.0f;
    style.FrameRounding = 2.0f;
    style.TouchExtraPadding = ImVec2(2.0f, 2.0f);
    style.TabBarBorderSize = 0.0f;
    style.TabRounding = 4.0f;
    style.DockingSeparatorSize = 0.0f;
}

// A window over the whole display, behind the panels, so mouse events reach the editor everywhere.
void beginMainWindow()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                         ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::Begin("Main Window", nullptr, windowFlags);
    ImGui::PopStyleVar(3);
}

// The layout file of 'project' as UTF-8, which ImGui expects; its folder is created. Empty if it can't be.
auto layoutFile(const std::filesystem::path& project) -> std::string
{
    const auto folder = project / ".aegis" / "editor";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    if (error)
    {
        log::warn("The editor layout is not kept: cannot create '{}' ({})", folder.string(), error.message());
        return {};
    }
    const auto file = (folder / "imgui.ini").u8string();
    return std::string{ file.begin(), file.end() };
}
}

EditorShell::EditorShell()
{
    m_panels.push_back(makeScenePanel());
    m_panels.push_back(makeRendererPanel());
    m_panels.push_back(makeStatisticsPanel());
    m_panels.push_back(makeProfilerPanel());
    m_panels.push_back(makeDemoPanel());
}

EditorShell::~EditorShell() = default;

auto EditorShell::onStartup(Engine& engine) -> std::expected<void, EngineError>
{
    applyStyle();
    // A headless run (a test, a screenshot) neither reads nor changes the user's layout.
    if (!engine.headless() && !engine.project().empty())
        m_layoutFile = layoutFile(engine.project());
    ImGui::GetIO().IniFilename = m_layoutFile.empty() ? nullptr : m_layoutFile.c_str();
    return {};
}

void EditorShell::onShutdown()
{
    // ImGui would save on destroying its context, after this shell (and the file name) are gone.
    if (m_layoutFile.empty())
        return;
    ImGui::SaveIniSettingsToDisk(m_layoutFile.c_str());
    ImGui::GetIO().IniFilename = nullptr;
}

auto EditorShell::renderView() const -> std::optional<RenderView>
{
    if (!m_viewportExtent)
        return std::nullopt;
    return RenderView{ .extent = *m_viewportExtent };
}

auto EditorShell::inputCapture() const -> InputCapture
{
    const auto& io = ImGui::GetIO();
    return { .keyboard = io.WantCaptureKeyboard, .mouse = io.WantCaptureMouse };
}

void EditorShell::onFrame(FrameContext& frame)
{
    m_context.setWorld(frame.world);
    m_context.selection().prune(frame.world.registry());

    ImGuizmo::BeginFrame();
    updateGizmoMode();

    beginMainWindow();
    drawMenuBar();
    for (const auto& panel : m_panels)
    {
        if (panel->isOpen())
            panel->draw(m_context, frame);
    }
    drawViewport(frame);
    if (m_gizmoMode != GizmoMode::None)
        drawGizmo(frame.world);
    ImGui::End();
}

void EditorShell::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("View"))
    {
        for (const auto& panel : m_panels)
        {
            const std::string name{ panel->name() };
            if (ImGui::MenuItem(name.c_str(), nullptr, panel->isOpen()))
                panel->setOpen(!panel->isOpen());
        }
        if (ImGui::MenuItem("Viewport", nullptr, m_viewportOpen))
            m_viewportOpen = !m_viewportOpen;
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

// A first viewport (docs/editor.md, E2 finishes it): the scene renders into a view of the panel's size, shown in it.
void EditorShell::drawViewport(FrameContext& frame)
{
    if (m_viewportOpen)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0.0f, 0.0f });
        const bool visible = ImGui::Begin("Viewport", &m_viewportOpen);
        ImGui::PopStyleVar();
        if (visible)
        {
            const auto size = ImGui::GetContentRegionAvail();
            m_viewportExtent = rhi::Extent2D{ static_cast<std::uint32_t>(std::max(size.x, 1.0f)), static_cast<std::uint32_t>(std::max(size.y, 1.0f)) };
            // The graph renders into the view the viewport asked for last frame; until then "Final" is the window's.
            const auto final = frame.frameGraph.declarations().find(render_graph::names::finalImage);
            if (frame.viewExtent && final)
            {
                if (const auto texture = frame.presentation.uiTexture(frame.frameGraph.pool().texture(*final).view))
                    ImGui::Image(ImTextureRef{ texture }, size);
            }
        }
        ImGui::End();
    }
    if (!m_viewportOpen)
        m_viewportExtent.reset();
}

void EditorShell::updateGizmoMode()
{
    // ImGui's keys, not the game's input: typing in a text field doesn't switch the gizmo
    const auto& io = ImGui::GetIO();
    m_snapping = io.KeyCtrl;
    if (ImGuizmo::IsUsing() || io.WantTextInput)
        return;

    if (ImGui::IsKeyDown(ImGuiKey_Q))
        m_gizmoMode = GizmoMode::None;
    else if (ImGui::IsKeyDown(ImGuiKey_W))
        m_gizmoMode = GizmoMode::Translate;
    else if (ImGui::IsKeyDown(ImGuiKey_E))
        m_gizmoMode = GizmoMode::Rotate;
    else if (ImGui::IsKeyDown(ImGuiKey_R))
        m_gizmoMode = GizmoMode::Scale;
}

void EditorShell::drawGizmo(world::World& world)
{
    auto& registry = world.registry();
    const auto selected = m_context.selectedEntity();
    const auto mainCamera = world.mainCamera();
    if (!registry.valid(selected) || !registry.has<Transform, GlobalTransform, Parent>(selected))
        return;
    if (!registry.has<Camera>(mainCamera)) // Null or without a camera
        return;

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    const ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    auto& camera = registry.get<Camera>(mainCamera);
    glm::mat4 projectionMatrix = camera.projectionMatrix;
    projectionMatrix[1][1] *= -1.0f; // Flip y-axis

    // The gizmo works in world space; the result is converted back to the local transform
    auto& transform = registry.get<Transform>(selected);
    glm::mat4 transformMatrix = registry.get<GlobalTransform>(selected).matrix();

    auto operation = ImGuizmo::OPERATION::TRANSLATE;
    float snapValue = 0.5f;
    if (m_gizmoMode == GizmoMode::Rotate)
    {
        operation = ImGuizmo::OPERATION::ROTATE;
        snapValue = 15.0f;
    }
    else if (m_gizmoMode == GizmoMode::Scale)
    {
        operation = ImGuizmo::OPERATION::SCALE;
        snapValue = 0.1f;
    }
    const std::array snapValues{ snapValue, snapValue, snapValue };

    ImGuizmo::Manipulate(glm::value_ptr(camera.viewMatrix), glm::value_ptr(projectionMatrix), operation,
        ImGuizmo::MODE::LOCAL, glm::value_ptr(transformMatrix), nullptr, m_snapping ? snapValues.data() : nullptr);

    if (ImGuizmo::IsUsing())
    {
        GlobalTransform parentGlobal{};
        const auto parent = registry.get<Parent>(selected).entity;
        if (parent && registry.has<GlobalTransform>(parent))
            parentGlobal = registry.get<GlobalTransform>(parent);

        transform = world::detail::toLocal(parentGlobal, transformMatrix);
        if (!registry.has<DynamicTag>(selected))
            registry.markStaticDirty(selected); // Static entities only move when told to
    }
}
}
