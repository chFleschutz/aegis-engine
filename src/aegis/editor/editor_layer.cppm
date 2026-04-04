module;

#include <ImGuizmo.h>

export module Aegis.Editor;

import Aegis.Math;
import Aegis.Core.Layer;
import Aegis.Core.Input;
import Aegis.Editor.Panels;

namespace Aegis::Editor
{
	class EditorLayer : public Core::Layer
	{
	public:
		EditorLayer() = default;

		virtual void onUpdate(float deltaSeconds) override
		{
			m_snapping = Input::instance().keyPressed(Input::LeftControl);

			if (!ImGuizmo::IsUsing())
			{
				if (Input::instance().keyPressed(Input::Q))
					m_gizmoType = -1;
				else if (Input::instance().keyPressed(Input::W))
					m_gizmoType = ImGuizmo::OPERATION::TRANSLATE;
				else if (Input::instance().keyPressed(Input::E))
					m_gizmoType = ImGuizmo::OPERATION::ROTATE;
				else if (Input::instance().keyPressed(Input::R))
					m_gizmoType = ImGuizmo::OPERATION::SCALE;
			}
		}

		virtual void onUIRender() override
		{
			m_menuBarPanel.draw();

			if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Renderer))
				m_rendererPanel.draw();

			if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Scene))
				m_scenePanel.draw();

			if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Statistics))
				m_statisticsPanel.draw();

			if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Profiler))
				m_profilerPanel.draw();

			if (m_menuBarPanel.flagActive(Editor::MenuBarPanel::Demo))
				m_demoPanel.draw();

			if (m_gizmoType != -1)
				drawGizmo();
		}

	private:
		void drawGizmo()
		{
			auto selected = m_scenePanel.selectedEntity();
			if (!selected)
				return;

			ImGuizmo::SetOrthographic(false);
			ImGuizmo::SetDrawlist();
			ImGuiIO& io = ImGui::GetIO();
			ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

			auto& scene = Engine::instance().scene();
			auto& camera = scene.mainCamera().get<Camera>();
			glm::mat4 projectionMatrix = camera.projectionMatrix;
			projectionMatrix[1][1] *= -1.0f; // Flip y-axis 

			auto& transform = selected.get<Transform>();
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