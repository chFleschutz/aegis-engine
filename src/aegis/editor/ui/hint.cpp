module;

#include <imgui.h>

module aegis.editor;
import std;
import :panels;

namespace aegis::editor
{
namespace
{
// 'text' broken into lines of at most 'width' pixels (a single longer word keeps its own line), at spaces.
auto wrapLines(const std::string& text, float width) -> std::vector<std::string>
{
    std::vector<std::string> lines;
    std::string line;
    std::istringstream words{ text };
    for (std::string word; words >> word;)
    {
        const std::string joined = line.empty() ? word : line + ' ' + word;
        if (!line.empty() && ImGui::CalcTextSize(joined.c_str()).x > width)
        {
            lines.push_back(std::move(line));
            line = word;
        }
        else
        {
            line = joined;
        }
    }
    if (!line.empty())
        lines.push_back(std::move(line));
    return lines;
}
}

void drawCenteredHint(std::string_view text)
{
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float margin = ImGui::GetFontSize() * 1.5f;
    const auto lines = wrapLines(std::string{ text }, std::max(available.x - 2.0f * margin, 1.0f));
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float height = static_cast<float>(lines.size()) * lineHeight - ImGui::GetStyle().ItemSpacing.y;

    const ImVec2 start = ImGui::GetCursorPos();
    float y = start.y + std::max((available.y - height) * 0.5f, 0.0f);
    for (const auto& line : lines)
    {
        const float width = ImGui::CalcTextSize(line.c_str()).x;
        ImGui::SetCursorPos(ImVec2{ start.x + std::max((available.x - width) * 0.5f, 0.0f), y });
        ImGui::TextDisabled("%s", line.c_str());
        y += lineHeight;
    }
    ImGui::SetCursorPos(start);
}
}
