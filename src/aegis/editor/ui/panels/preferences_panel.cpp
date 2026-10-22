module;

#include <imgui.h>

module aegis.editor;
import std;
import :panels;

namespace aegis::editor
{
namespace
{
// The user's editor preferences (EditorContext::preferences); the shell saves them when they change.
class PreferencesPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "Preferences"; }

    void draw(EditorContext& editor, FrameContext&) override
    {
        bool open = true;
        const bool visible = ImGui::Begin("Preferences", &open);
        setOpen(open);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        auto& preferences = editor.preferences();
        ImGui::DragFloat("Camera speed", &preferences.cameraSpeed, 0.1f, 0.01f, 1000.0f, "%.2f units/s");
        ImGui::DragFloat("Snap move", &preferences.snapTranslate, 0.01f, 0.001f, 1000.0f, "%.3f units");
        ImGui::DragFloat("Snap rotate", &preferences.snapRotate, 0.1f, 0.01f, 180.0f, "%.2f deg");
        ImGui::DragFloat("Snap scale", &preferences.snapScale, 0.01f, 0.001f, 100.0f, "%.3f");
        ImGui::DragFloat("UI scale", &preferences.uiScale, 0.01f, 0.5f, 4.0f, "%.2f");

        if (!preferences.recentProjects.empty())
        {
            ImGui::SeparatorText("Recent projects");
            for (const auto& project : preferences.recentProjects)
                ImGui::TextUnformatted(project.c_str());
        }
        ImGui::End();
    }
};
}

auto makePreferencesPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<PreferencesPanel>();
}
}
