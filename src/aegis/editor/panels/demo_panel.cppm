module;

#include <imgui.h>

export module Aegis.Editor.Panels:DemoPanel;

export namespace Aegis::Editor
{
	/// @brief A demo layer that shows the ImGui demo window
	class DemoPanel
	{
	public:
		void draw()
		{
			ImGui::ShowDemoWindow();
		}
	};
}