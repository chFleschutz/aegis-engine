module;

#include <imgui.h>
#include <ImGuizmo.h>

export module aegis.editor;

import std;
import aegis.math;
import aegis.editor.panels;
import aegis.engine;
import aegis.render_graph;
import aegis.renderer;
import aegis.world;
import aegis.world.systems.transform_system;

export namespace aegis::editor
{
class EditorShell : public Shell
{
public:
    [[nodiscard]] auto updateMode() const -> world::UpdateMode override { return world::UpdateMode::Edit; }

    [[nodiscard]] auto inputCapture() const -> InputCapture override
    {
        const auto& io = ImGui::GetIO();
        return { .keyboard = io.WantCaptureKeyboard, .mouse = io.WantCaptureMouse };
    }

    void onFrame(FrameContext& frame) override
    {
        ImGuizmo::BeginFrame();
        updateGizmoMode();

        // Create full screen window to receive mouse events everywhere
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Main Window", nullptr, windowFlags);
        ImGui::PopStyleVar(3);

        m_menuBarPanel.draw();

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Renderer))
            m_rendererPanel.draw(frame.renderer, frame.frameGraph);

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Scene))
            m_scenePanel.draw(frame.world);

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Statistics))
            m_statisticsPanel.draw(frame.world.registry(), frame.instances);

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Profiler))
            m_profilerPanel.draw(frame.frameGraph);

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Demo))
            m_demoPanel.draw();

        if (m_gizmoType != -1)
            drawGizmo(frame.world);

        ImGui::End();
    }

private:
    void updateGizmoMode()
    {
        // ImGui's keys, not the game's input: typing in a text field doesn't switch the gizmo
        const auto& io = ImGui::GetIO();
        m_snapping = io.KeyCtrl;
        if (ImGuizmo::IsUsing() || io.WantTextInput)
            return;

        if (ImGui::IsKeyDown(ImGuiKey_Q))
            m_gizmoType = -1;
        else if (ImGui::IsKeyDown(ImGuiKey_W))
            m_gizmoType = ImGuizmo::OPERATION::TRANSLATE;
        else if (ImGui::IsKeyDown(ImGuiKey_E))
            m_gizmoType = ImGuizmo::OPERATION::ROTATE;
        else if (ImGui::IsKeyDown(ImGuiKey_R))
            m_gizmoType = ImGuizmo::OPERATION::SCALE;
    }

    void drawGizmo(world::World& scene)
    {
        auto& registry = scene.registry();
        const auto selected = m_scenePanel.selectedEntity();
        const auto mainCamera = scene.mainCamera();
        if (!registry.valid(selected) || !registry.has<Transform, GlobalTransform, Parent>(selected))
            return;
        if (!registry.has<Camera>(mainCamera)) // Null or without a camera
            return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuiIO& io = ImGui::GetIO();
        ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

        auto& camera = registry.get<Camera>(mainCamera);
        glm::mat4 projectionMatrix = camera.projectionMatrix;
        projectionMatrix[1][1] *= -1.0f; // Flip y-axis

        // The gizmo works in world space; the result is converted back to the local transform
        auto& transform = registry.get<Transform>(selected);
        glm::mat4 transformMatrix = registry.get<GlobalTransform>(selected).matrix();

        float snapValue = 0.5f;
        if (m_gizmoType == ImGuizmo::OPERATION::SCALE)
            snapValue = 0.1f;
        if (m_gizmoType == ImGuizmo::OPERATION::ROTATE)
            snapValue = 15.0f;

        float snapValues[3] = { snapValue, snapValue, snapValue };

        ImGuizmo::Manipulate(glm::value_ptr(camera.viewMatrix), glm::value_ptr(projectionMatrix),
            static_cast<ImGuizmo::OPERATION>(m_gizmoType), ImGuizmo::MODE::LOCAL, glm::value_ptr(transformMatrix),
            nullptr, m_snapping ? snapValues : nullptr);

        if (ImGuizmo::IsUsing())
        {
            GlobalTransform parentGlobal{};
            auto parent = registry.get<Parent>(selected).entity;
            if (parent && registry.has<GlobalTransform>(parent))
                parentGlobal = registry.get<GlobalTransform>(parent);

            transform = world::detail::toLocal(parentGlobal, transformMatrix);
            if (!registry.has<DynamicTag>(selected))
                registry.markStaticDirty(selected); // Static entities only move when told to
        }
    }

    editor::MenuBarPanel m_menuBarPanel{};
    editor::RendererPanel m_rendererPanel{};
    editor::ScenePanel m_scenePanel{};
    editor::StatisticsPanel m_statisticsPanel{};
    editor::ProfilerPanel m_profilerPanel{};
    editor::DemoPanel m_demoPanel{};
    int m_gizmoType = -1;
    bool m_snapping = false;
};
}