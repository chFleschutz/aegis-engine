module;

#include <imgui.h>

module aegis.editor;
import std;
import :panels;

namespace aegis::editor
{
namespace
{
// The viewport's own keys and mouse controls, which are not actions: they work while it is focused or hovered.
struct ViewportControl
{
    const char* input;
    const char* does;
};

constexpr std::array viewportControls{
    ViewportControl{ "Q / W / E / R", "Select, Move, Rotate, Scale (the gizmo)" },
    ViewportControl{ "Ctrl (dragging the gizmo)", "Snap, or not while snapping is on" },
    ViewportControl{ "Left click", "Select" },
    ViewportControl{ "Right mouse + drag", "Look around" },
    ViewportControl{ "Right mouse + W / A / S / D", "Fly" },
    ViewportControl{ "Right mouse + Q / E", "Fly down, up" },
    ViewportControl{ "Right mouse + wheel", "Flying speed" },
    ViewportControl{ "Alt + left mouse", "Orbit" },
    ViewportControl{ "Middle mouse", "Pan" },
    ViewportControl{ "Wheel", "Dolly" },
};

// Help > Keyboard Shortcuts: the actions that have a chord, as the registry binds them, and the viewport's controls,
// in a floating window of its own.
class ShortcutsPanel final : public Panel
{
public:
    explicit ShortcutsPanel(const ActionRegistry& actions) :
        m_actions{ actions }
    {
    }

    [[nodiscard]] auto name() const -> std::string_view override { return "Keyboard Shortcuts"; }

    void draw(EditorContext&, FrameContext&) override
    {
        bool open = true;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2{ 0.5f, 0.5f });
        const bool visible = ImGui::Begin("Keyboard Shortcuts", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
        setOpen(open);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Editor");
        if (beginTable("##actions"))
        {
            for (const auto& action : m_actions.actions())
            {
                if (!action.chord)
                    continue;
                auto keys = describe(*action.chord);
                if (action.alternate)
                    keys += std::format(", {}", describe(*action.alternate));
                std::string_view label = action.label;
                if (label.ends_with("..."))
                    label.remove_suffix(3);
                row(label, keys);
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Viewport");
        if (beginTable("##viewport"))
        {
            for (const auto& control : viewportControls)
                row(control.does, control.input);
            ImGui::EndTable();
        }
        ImGui::End();
    }

private:
    static auto beginTable(const char* id) -> bool
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            return false;
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Keys");
        return true;
    }

    static void row(std::string_view does, std::string_view keys)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(does.data(), does.data() + does.size());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%.*s", static_cast<int>(keys.size()), keys.data());
    }

    const ActionRegistry& m_actions;
};
}

auto makeShortcutsPanel(const ActionRegistry& actions) -> std::unique_ptr<Panel>
{
    return std::make_unique<ShortcutsPanel>(actions);
}
}
