module;

#include <imgui.h>

export module aegis.editor.panels:demo_panel;

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