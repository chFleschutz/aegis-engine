module;

#include <imgui.h>

export module Aegis.UI.Panels:DemoPanel;

export namespace Aegis::UI
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