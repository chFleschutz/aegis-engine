module;

#include <imgui.h>
#include <ImGuizmo.h>

export module Aegis.UI;

import Aegis.Core.LayerStack;

export namespace Aegis::UI
{
	/// @brief Manages all GUI Layers for displaying ImGui elements
	/// @note This class is a wrapper around ImGui
	class UI
	{
	public:
		UI(Core::LayerStack& layerStack)
			: m_layerStack{ layerStack }
		{
			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			ImGui::StyleColorsDark();

			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.IniFilename = nullptr;
#ifndef NDEBUG
			io.ConfigDebugIsDebuggerPresent = true;
#endif // !NDEBUG

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
		
		UI(const UI&) = delete;
		UI(UI&&) = delete;
		~UI() = default;

		UI& operator=(const UI&) = delete;
		UI& operator=(UI&&) = delete;

		/// @brief Renders all GUI elements
		void render()
		{
			ImGui::NewFrame();
			ImGuizmo::BeginFrame();

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

			for (auto& layer : m_layerStack)
			{
				layer->onUIRender();
			}
			
			ImGui::End();
			ImGui::EndFrame();
		}

	private:
		Core::LayerStack& m_layerStack;
	};
}