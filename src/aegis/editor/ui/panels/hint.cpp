module;

#include <imgui.h>

module aegis.editor;
import std;
import :panels;

namespace aegis::editor
{
void drawCenteredHint(std::string_view text)
{
    const std::string line{ text }; // ImGui wants a terminated string
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 size = ImGui::CalcTextSize(line.c_str(), nullptr, false, available.x);
    const ImVec2 start = ImGui::GetCursorPos();
    const ImVec2 at{ start.x + std::max((available.x - size.x) * 0.5f, 0.0f), start.y + std::max((available.y - size.y) * 0.5f, 0.0f) };

    ImGui::SetCursorPos(at);
    ImGui::PushTextWrapPos(at.x + size.x + 1.0f); // The measured width, so the lines break where they were measured
    ImGui::TextDisabled("%s", line.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SetCursorPos(start);
}
}
