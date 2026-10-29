module;

#include <imgui.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import :panels;

namespace aegis::editor
{
namespace
{
// The user's editor preferences (EditorContext::preferences) in a floating window of its own, opened from Edit >
// Preferences...; the shell saves them when they change.
class PreferencesPanel final : public Panel
{
public:
    [[nodiscard]] auto name() const -> std::string_view override { return "Preferences"; }

    void draw(EditorContext& editor, FrameContext&) override
    {
        bool open = true;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2{ 0.5f, 0.5f });
        const bool visible = ImGui::Begin("Preferences", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
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
        drawUiScale(preferences.uiScale);

        if (!preferences.recentProjects.empty())
        {
            ImGui::SeparatorText("Recent projects");
            for (const auto& project : preferences.recentProjects)
                ImGui::TextUnformatted(project.c_str());
        }
        ImGui::End();
    }

private:
    // A combo of steps: only a choice changes the scale, so the widget doesn't move while it is open. A value from a
    // hand-edited file shows as custom. The effective scale next to it includes the monitor's.
    static void drawUiScale(float& uiScale)
    {
        const auto current = uiScaleStep(uiScale);
        if (ImGui::BeginCombo("UI scale", uiScaleLabel(uiScale).c_str()))
        {
            for (std::size_t i = 0; i < uiScaleSteps.size(); ++i)
            {
                const bool selected = current == i;
                if (ImGui::Selectable(uiScaleLabel(uiScaleSteps[i]).c_str(), selected))
                    uiScale = uiScaleSteps[i];
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", effectiveScaleLabel(theme::contentScale(), uiScale).c_str());
    }
};
}

auto makePreferencesPanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<PreferencesPanel>();
}
}
