module;

#include <imgui.h>
#include <ImGuizmo.h>

export module Aegis.Editor;

import Aegis.Math;
import Aegis.Core.Layer;
import Aegis.Editor.Panels;
import aegis.platform;
import aegis.render_graph;
import aegis.renderer;
import Aegis.Scene;

export namespace Aegis::Editor
{
class EditorLayer : public Core::Layer
{
public:
    EditorLayer(aegis::platform::Input& input, aegis::renderer::Renderer& renderer,
        aegis::render_graph::FrameGraph& frameGraph, aegis::render_graph::DrawBatchRegistry& drawBatches,
        Scene::Scene& scene) :
        m_input(input),
        m_renderer(renderer),
        m_frameGraph(frameGraph),
        m_drawBatches(drawBatches),
        m_scene(scene)
    {
    }

    virtual void onUpdate(float deltaSeconds) override
    {
        m_snapping = m_input.keyPressed(aegis::platform::Key::LeftControl);

        if (!ImGuizmo::IsUsing())
        {
            if (m_input.keyPressed(aegis::platform::Key::Q))
                m_gizmoType = -1;
            else if (m_input.keyPressed(aegis::platform::Key::W))
                m_gizmoType = ImGuizmo::OPERATION::TRANSLATE;
            else if (m_input.keyPressed(aegis::platform::Key::E))
                m_gizmoType = ImGuizmo::OPERATION::ROTATE;
            else if (m_input.keyPressed(aegis::platform::Key::R))
                m_gizmoType = ImGuizmo::OPERATION::SCALE;
        }
    }

    virtual void onUIRender() override
    {
        m_menuBarPanel.draw();

        if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Renderer))
            m_rendererPanel.draw(m_renderer, m_frameGraph);

        if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Scene))
            m_scenePanel.draw(m_scene);

        if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Statistics))
            m_statisticsPanel.draw(m_scene.registry(), m_drawBatches);

        if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Profiler))
            m_profilerPanel.draw(m_frameGraph);

        if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Demo))
            m_demoPanel.draw();

        if (m_gizmoType != -1)
            drawGizmo(m_scene);
    }

private:
    void drawGizmo(Scene::Scene& scene)
    {
        auto selected = m_scenePanel.selectedEntity();
        if (!selected)
            return;

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuiIO& io = ImGui::GetIO();
        ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

        auto& registry = scene.registry();
        auto& camera = registry.get<Camera>(scene.mainCamera());
        glm::mat4 projectionMatrix = camera.projectionMatrix;
        projectionMatrix[1][1] *= -1.0f; // Flip y-axis

        auto& transform = registry.get<Transform>(selected);
        glm::mat4 transformMatrix = transform.matrix();

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
            Math::decomposeTRS(transformMatrix, transform.location, transform.rotation, transform.scale);
        }
    }

    aegis::platform::Input& m_input;
    aegis::renderer::Renderer& m_renderer;
    aegis::render_graph::FrameGraph& m_frameGraph;
    aegis::render_graph::DrawBatchRegistry& m_drawBatches;
    Scene::Scene& m_scene;
    Editor::MenuBarPanel m_menuBarPanel{};
    Editor::RendererPanel m_rendererPanel{};
    Editor::ScenePanel m_scenePanel{};
    Editor::StatisticsPanel m_statisticsPanel{};
    Editor::ProfilerPanel m_profilerPanel{};
    Editor::DemoPanel m_demoPanel{};
    int m_gizmoType = -1;
    bool m_snapping = false;
};
}