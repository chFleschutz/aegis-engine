module;

#include <imgui.h>
#include <ImGuizmo.h>

export module aegis.editor;

import aegis.math;
import aegis.core.layer;
import aegis.editor.panels;
import aegis.platform;
import aegis.render_graph;
import aegis.renderer;
import aegis.scene;
import aegis.scene.systems.transform_system;

export namespace aegis::editor
{
class EditorLayer : public core::Layer
{
public:
    EditorLayer(aegis::platform::Input& input, aegis::renderer::Renderer& renderer,
        aegis::render_graph::FrameGraph& frameGraph, aegis::render_graph::DrawBatchRegistry& drawBatches,
        scene::Scene& scene) :
        m_input(input),
        m_renderer(renderer),
        m_frameGraph(frameGraph),
        m_drawBatches(drawBatches),
        m_scene(scene)
    {
    }

    virtual void onUpdate(float deltaSeconds) override
    {
        m_snapping = m_input.state().keyDown(aegis::platform::Key::LeftControl);

        if (!ImGuizmo::IsUsing())
        {
            if (m_input.state().keyDown(aegis::platform::Key::Q))
                m_gizmoType = -1;
            else if (m_input.state().keyDown(aegis::platform::Key::W))
                m_gizmoType = ImGuizmo::OPERATION::TRANSLATE;
            else if (m_input.state().keyDown(aegis::platform::Key::E))
                m_gizmoType = ImGuizmo::OPERATION::ROTATE;
            else if (m_input.state().keyDown(aegis::platform::Key::R))
                m_gizmoType = ImGuizmo::OPERATION::SCALE;
        }
    }

    virtual void onUIRender() override
    {
        m_menuBarPanel.draw();

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Renderer))
            m_rendererPanel.draw(m_renderer, m_frameGraph);

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Scene))
            m_scenePanel.draw(m_scene);

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Statistics))
            m_statisticsPanel.draw(m_scene.registry(), m_drawBatches);

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Profiler))
            m_profilerPanel.draw(m_frameGraph);

        if (m_menuBarPanel.flagActive(editor::MenuBarPanel::Demo))
            m_demoPanel.draw();

        if (m_gizmoType != -1)
            drawGizmo(m_scene);
    }

private:
    void drawGizmo(scene::Scene& scene)
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

            transform = scene::detail::toLocal(parentGlobal, transformMatrix);
            if (!registry.has<DynamicTag>(selected))
                registry.markStaticDirty(selected); // Static entities only move when told to
        }
    }

    aegis::platform::Input& m_input;
    aegis::renderer::Renderer& m_renderer;
    aegis::render_graph::FrameGraph& m_frameGraph;
    aegis::render_graph::DrawBatchRegistry& m_drawBatches;
    scene::Scene& m_scene;
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