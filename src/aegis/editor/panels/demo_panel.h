#pragma once

#include "imgui.h"

namespace Aegis::UI
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