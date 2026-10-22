module;

#include <cfloat>

#include <imgui.h>

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
        return ImVec4{ 0.78f, 0.78f, 0.78f, 1.0f };
    case log::Level::Warn:
        return ImVec4{ 1.0f, 0.82f, 0.3f, 1.0f };
    case log::Level::Error:
    case log::Level::Fatal:
        return ImVec4{ 1.0f, 0.4f, 0.4f, 1.0f };
    }
    std::unreachable();
}

// The log lines of the editor (EditorContext::console) with level toggles, a search, Clear and Copy.
class ConsolePanel final : public Panel
{
public:
    ConsolePanel() { setOpen(true); }

    [[nodiscard]] auto name() const -> std::string_view override { return "Console"; }

    void draw(EditorContext& editor, FrameContext&) override
    {
        bool open = true;
        const bool visible = ImGui::Begin("Console", &open);
        setOpen(open);
        if (!visible)
        {
            ImGui::End();
            return;
        }

        auto& console = editor.console();
        ImGui::Checkbox("Info", &m_filter.info);
        ImGui::SameLine();
        ImGui::Checkbox("Warn", &m_filter.warn);
        ImGui::SameLine();
        ImGui::Checkbox("Error", &m_filter.error);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
            console.clear();
        ImGui::SameLine();
        if (ImGui::Button("Copy"))
            ImGui::SetClipboardText(console.text(m_filter).c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", "Search", &m_search[0], m_search.size());
        m_filter.search = m_search.c_str();

        ImGui::Separator();
        if (ImGui::BeginChild("Lines", ImVec2{ 0.0f, 0.0f }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        {
            const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
            for (const auto& entry : console.entries(m_filter))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
                ImGui::TextUnformatted(std::format("[{}] {}", log::levelName(entry.level), entry.message).c_str());
                ImGui::PopStyleColor();
            }
            if (atBottom) // Follows new lines until the user scrolls up
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

private:
    ConsoleFilter m_filter;
    std::string m_search = std::string(256, '\0'); // The search field's text, zero-terminated
};
}

auto makeConsolePanel() -> std::unique_ptr<Panel>
{
    return std::make_unique<ConsolePanel>();
}
}
