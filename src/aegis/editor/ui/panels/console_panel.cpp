module;

#include <cfloat>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "aegis/editor/ui/theme.h"

module aegis.editor;
import std;
import aegis.core;
import :panels;

namespace aegis::editor
{
namespace
{
auto levelColor(log::Level level) -> ImVec4
{
    switch (level)
    {
    case log::Level::Info:
        return theme::textDim;
    case log::Level::Warn:
        return theme::warning;
    case log::Level::Error:
    case log::Level::Fatal:
        return theme::error;
    }
    std::unreachable();
}

// A button showing whether 'on' is set, which flips it. A '###id' in 'label' keeps the ID while the text changes.
void toggleButton(const std::string& label, bool& on)
{
    const bool wasOn = on;
    if (wasOn)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::accentActive);
    }
    if (ImGui::Button(label.c_str()))
        on = !on;
    if (wasOn)
        ImGui::PopStyleColor(3);
}

// The repeat count ("x25") after a line that stands for 'repeats' messages (Collapse).
void drawRepeats(std::size_t repeats)
{
    const auto label = std::format("\u00D7{}", repeats);
    const float padding = 4.0f * theme::scale();
    ImGui::SameLine();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::CalcTextSize(label.c_str());
    ImGui::GetWindowDrawList()->AddRectFilled(position, ImVec2{ position.x + size.x + 2.0f * padding, position.y + size.y }, theme::packed(theme::bgFrameActive), size.y * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2{ position.x + padding, position.y });
    ImGui::TextUnformatted(label.c_str());
}

// The log lines of the editor (EditorContext::console) with level toggles and their counts, Collapse, a search, Clear
// and Copy. The status bar's counts bring it to the front with only the warnings and errors on.
class ConsolePanel final : public Panel
{
public:
    ConsolePanel() { setOpen(true); }

    [[nodiscard]] auto name() const -> std::string_view override { return "Console"; }

    void draw(EditorContext& editor, FrameContext&) override
    {
        if (editor.takeConsoleProblemsRequest())
        {
            m_filter.info = false;
            m_filter.warn = true;
            m_filter.error = true;
            ImGui::SetNextWindowFocus(); // Also selects its tab when docked
        }

        bool open = true;
        const bool visible = ImGui::Begin("Console", &open);
        setOpen(open);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        auto& console = editor.console();
        const auto counts = console.counts();
        toggleButton(std::format("Info {}###info", counts.info), m_filter.info);
        ImGui::SameLine();
        toggleButton(std::format("Warn {}###warn", counts.warnings), m_filter.warn);
        ImGui::SameLine();
        toggleButton(std::format("Error {}###error", counts.errors), m_filter.error);
        ImGui::SameLine();
        toggleButton("Collapse", m_filter.collapse);
        ImGui::SetItemTooltip("Merges repeated messages, also those that differ only in their numbers");
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            console.clear();
        ImGui::SameLine();
        if (ImGui::Button("Copy"))
            ImGui::SetClipboardText(console.text(m_filter).c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", "Search", &m_filter.search);

        ImGui::Separator();
        if (ImGui::BeginChild("Lines", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        {
            const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
            const theme::MonospaceScope mono;
            for (const auto& entry : console.entries(m_filter))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
                ImGui::TextUnformatted(std::format("[{}] {}", log::levelName(entry.level), entry.message).c_str());
                ImGui::PopStyleColor();
                if (entry.repeats > 1)
                    drawRepeats(entry.repeats);
            }
            if (atBottom) // Follows new lines until the user scrolls up
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

private:
    ConsoleFilter m_filter;
};
}

auto makeConsolePanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<ConsolePanel>();
}
}
